/*
 * xbased native core — achievements (cheevos.h): local achievement files run
 * by rcheevos' runtime, and the memory search that finds their addresses.
 */
#include "native/core/cheevos.h"
#include "native/core/vm.h"
#include "native/core/nc.h"

/* mods.c: the game's memory once bound */
NcVm *nc_mods_vm(void);

#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef NC_HAVE_RCHEEVOS
#include "rc_runtime.h"
#include "rc_error.h"
#endif


#define TAG "xbased-cheevos"

typedef struct {
    char name[96];
    char desc[200];
    char trigger[1024];
    char error[96];
    int state;              /* 1 unlocked, 0 locked, -1 no working trigger */
} Cheevo;

static char g_exe_dir[1024], g_data_dir[1024];
static uint32_t g_title;
static Cheevo g_c[NC_CHEEVOS_MAX];
static unsigned g_n;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static char g_pending[8][96];   /* unlocks for the app's notice */
static unsigned g_pend_head, g_pend_tail;
static bool g_dirty;            /* reload asked: on the guest's thread next frame */
#ifdef NC_HAVE_RCHEEVOS
static rc_runtime_t g_rt;
static bool g_rt_up;
#endif

void nc_cheevos_set_dirs(const char *exe_dir, const char *data_dir)
{
    snprintf(g_exe_dir, sizeof(g_exe_dir), "%s", exe_dir ? exe_dir : "");
    snprintf(g_data_dir, sizeof(g_data_dir), "%s", data_dir ? data_dir : "");
}

static void trim(char *s)
{
    char *p = s;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    memmove(s, p, strlen(p) + 1);
    for (size_t n = strlen(s); n && isspace((unsigned char)s[n - 1]); n--) {
        s[n - 1] = 0;
    }
}

static Cheevo *by_name(const char *name)
{
    for (unsigned i = 0; i < g_n; i++) {
        if (!strcmp(g_c[i].name, name)) {
            return &g_c[i];
        }
    }
    return NULL;
}

/* One file's achievements: [name], desc:, trigger: (the rest of a line after # ignored outside a trigger) */
static void read_defs(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        return;
    }
    char line[1400];
    Cheevo *c = NULL;
    unsigned got = 0;
    while (fgets(line, sizeof(line), f)) {
        trim(line);
        if (!line[0] || line[0] == '#' || line[0] == ';') {
            continue;
        }
        if (line[0] == '[') {
            char *e = strrchr(line, ']');
            if (e) {
                *e = 0;
            }
            char name[96];
            snprintf(name, sizeof(name), "%s", line + 1);
            trim(name);
            c = by_name(name);
            if (!c && g_n < NC_CHEEVOS_MAX) {
                c = &g_c[g_n++];
            }
            if (c) {
                memset(c, 0, sizeof(*c));
                snprintf(c->name, sizeof(c->name), "%s", name);
                got++;
            }
        } else if (c && !strncmp(line, "desc:", 5)) {
            snprintf(c->desc, sizeof(c->desc), "%s", line + 5);
            trim(c->desc);
        } else if (c && !strncmp(line, "trigger:", 8)) {
            snprintf(c->trigger, sizeof(c->trigger), "%s", line + 8);
            trim(c->trigger);
        }
    }
    fclose(f);
    nc_log(TAG, "%s: %u achievements", path, got);
}

static void unlocked_path(char *out, size_t n)
{
    snprintf(out, n, "%sachievements/%08X.unlocked", g_data_dir, g_title);
}

static void read_unlocked(void)
{
    char p[1200], line[200];
    unlocked_path(p, sizeof(p));
    FILE *f = fopen(p, "r");
    if (!f) {
        return;
    }
    while (fgets(line, sizeof(line), f)) {
        trim(line);
        Cheevo *c = by_name(line);
        if (c && c->state == 0) {
            c->state = 1;
        }
    }
    fclose(f);
}

static void save_unlock(const char *name)
{
    char p[1200];
    snprintf(p, sizeof(p), "%sachievements", g_data_dir);
    mkdir(p, 0775);     /* the platform's own on Windows (the build maps it) */
    unlocked_path(p, sizeof(p));
    FILE *f = fopen(p, "a");
    if (f) {
        fprintf(f, "%s\n", name);
        fclose(f);
    }
}

/* the runtime set up for the achievements still locked (guest thread) */
static void load_all(void)
{
    g_n = 0;
    char p[1200];
    snprintf(p, sizeof(p), "%sachievements/%08X.txt", g_exe_dir, g_title);
    read_defs(p);
    if (strcmp(g_exe_dir, g_data_dir)) {
        snprintf(p, sizeof(p), "%sachievements/%08X.txt", g_data_dir, g_title);
        read_defs(p);
    }
#ifdef NC_HAVE_RCHEEVOS
    if (g_rt_up) {
        rc_runtime_destroy(&g_rt);
    }
    rc_runtime_init(&g_rt);
    g_rt_up = true;
#endif
    unsigned live = 0;
    for (unsigned i = 0; i < g_n; i++) {
        Cheevo *c = &g_c[i];
        c->state = 0;
        c->error[0] = 0;
        if (!c->trigger[0]) {
            c->state = -1;
            snprintf(c->error, sizeof(c->error), "no trigger yet");
            continue;
        }
#ifdef NC_HAVE_RCHEEVOS
        const int r = rc_runtime_activate_achievement(&g_rt, i + 1, c->trigger, NULL, 0);
        if (r != RC_OK) {
            c->state = -1;
            snprintf(c->error, sizeof(c->error), "%s", rc_error_str(r));
            nc_log(TAG, "%s: trigger \"%s\": %s", c->name, c->trigger, c->error);
        }
#else
        c->state = -1;
        snprintf(c->error, sizeof(c->error), "built without rcheevos");
#endif
    }
    read_unlocked();
    for (unsigned i = 0; i < g_n; i++) {
#ifdef NC_HAVE_RCHEEVOS
        if (g_c[i].state == 1) {
            rc_runtime_deactivate_achievement(&g_rt, i + 1);
        }
#endif
        live += g_c[i].state == 0;
    }
    nc_log(TAG, "title %08X: %u achievements, %u to unlock", g_title, g_n, live);
}

void nc_cheevos_bind(uint32_t title_id)
{
    pthread_mutex_lock(&g_lock);
    g_title = title_id;
    load_all();
    pthread_mutex_unlock(&g_lock);
}

void nc_cheevos_reload(void)
{
    g_dirty = true;
}

void nc_cheevos_relock_all(void)
{
    pthread_mutex_lock(&g_lock);
    char p[1200];
    unlocked_path(p, sizeof(p));
    remove(p);
    pthread_mutex_unlock(&g_lock);
    g_dirty = true;
}

#ifdef NC_HAVE_RCHEEVOS
static NcCheevosRead g_read;

static uint32_t RC_CCONV peek(uint32_t address, uint32_t num_bytes, void *ud)
{
    (void)ud;
    uint8_t b[4] = {0};
    if (num_bytes > 4 || !g_read(address, b, num_bytes)) {
        return 0;
    }
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}

static void RC_CCONV on_event(const rc_runtime_event_t *e)
{
    if (e->type != RC_RUNTIME_EVENT_ACHIEVEMENT_TRIGGERED || !e->id || e->id > g_n) {
        return;
    }
    Cheevo *c = &g_c[e->id - 1];
    if (c->state != 0) {
        return;
    }
    c->state = 1;
    rc_runtime_deactivate_achievement(&g_rt, e->id);
    save_unlock(c->name);
    nc_log(TAG, "unlocked: %s", c->name);
    if (g_pend_head - g_pend_tail < 8) {
        snprintf(g_pending[g_pend_head % 8], sizeof(g_pending[0]), "%s", c->name);
        g_pend_head++;
    }
}
#endif

void nc_cheevos_frame(NcCheevosRead read)
{
    pthread_mutex_lock(&g_lock);
    if (g_dirty && g_title) {
        g_dirty = false;
        load_all();
    }
#ifdef NC_HAVE_RCHEEVOS
    if (g_rt_up && g_n) {
        g_read = read;
        rc_runtime_do_frame(&g_rt, on_event, peek, NULL, NULL);
    }
#else
    (void)read;
#endif
    pthread_mutex_unlock(&g_lock);
}

unsigned nc_cheevos_count(void)
{
    return g_n;
}

const char *nc_cheevos_name(unsigned i)
{
    return i < g_n ? g_c[i].name : "";
}

const char *nc_cheevos_desc(unsigned i)
{
    return i < g_n ? g_c[i].desc : "";
}

int nc_cheevos_state(unsigned i)
{
    return i < g_n ? g_c[i].state : -1;
}

const char *nc_cheevos_error(unsigned i)
{
    return i < g_n ? g_c[i].error : "";
}

bool nc_cheevos_take_unlock(char *name, size_t n)
{
    pthread_mutex_lock(&g_lock);
    const bool any = g_pend_tail != g_pend_head;
    if (any) {
        snprintf(name, n, "%s", g_pending[g_pend_tail % 8]);
        g_pend_tail++;
    }
    pthread_mutex_unlock(&g_lock);
    return any;
}

/* ------------------------------------------------------------ memory search */

static uint8_t *g_prev;         /* RAM at the last filter */
static uint8_t *g_cand;         /* a bit an aligned address: still a candidate */
static uint32_t g_ms_ram, g_ms_count;
static unsigned g_ms_size;

static uint32_t val_at(const uint8_t *m, uint32_t pa, unsigned size)
{
    uint32_t v = m[pa];
    if (size >= 2) {
        v |= (uint32_t)m[pa + 1] << 8;
    }
    if (size == 4) {
        v |= (uint32_t)m[pa + 2] << 16 | (uint32_t)m[pa + 3] << 24;
    }
    return v;
}

bool nc_msearch_new(unsigned size)
{
    NcVm *vm = nc_mods_vm();
    if (!vm || (size != 1 && size != 2 && size != 4)) {
        return false;
    }
    if (g_ms_ram != vm->ram_size) {
        free(g_prev);
        free(g_cand);
        g_prev = malloc(vm->ram_size);
        g_cand = malloc(vm->ram_size / 8 + 1);
        g_ms_ram = g_prev && g_cand ? vm->ram_size : 0;
        if (!g_ms_ram) {
            return false;
        }
    }
    g_ms_size = size;
    memcpy(g_prev, vm->phys, g_ms_ram);
    memset(g_cand, 0, g_ms_ram / 8 + 1);
    g_ms_count = 0;
    for (uint32_t pa = 0; pa + size <= g_ms_ram; pa += size) {
        g_cand[pa / 8] |= (uint8_t)(1u << (pa % 8));
        g_ms_count++;
    }
    nc_log(TAG, "memory search: %u-bit, %u addresses", size * 8, g_ms_count);
    return true;
}

uint32_t nc_msearch_filter(int op, uint32_t value)
{
    NcVm *vm = nc_mods_vm();
    if (!vm || !g_ms_size || vm->ram_size != g_ms_ram) {
        return 0;
    }
    const uint32_t mask = g_ms_size == 4 ? 0xFFFFFFFFu : (1u << (g_ms_size * 8)) - 1;
    value &= mask;
    g_ms_count = 0;
    for (uint32_t pa = 0; pa + g_ms_size <= g_ms_ram; pa += g_ms_size) {
        uint8_t *b = &g_cand[pa / 8];
        const uint8_t bit = (uint8_t)(1u << (pa % 8));
        if (!(*b & bit)) {
            continue;
        }
        const uint32_t now = val_at(vm->phys, pa, g_ms_size), was = val_at(g_prev, pa, g_ms_size);
        bool keep;
        switch (op) {
        case NC_MS_EQUAL: keep = now == value; break;
        case NC_MS_CHANGED: keep = now != was; break;
        case NC_MS_UNCHANGED: keep = now == was; break;
        case NC_MS_INCREASED: keep = now > was; break;
        case NC_MS_DECREASED: keep = now < was; break;
        default: keep = true;
        }
        if (keep) {
            g_ms_count++;
        } else {
            *b &= (uint8_t)~bit;
        }
    }
    memcpy(g_prev, vm->phys, g_ms_ram);
    nc_log(TAG, "memory search: %u left", g_ms_count);
    return g_ms_count;
}

bool nc_msearch_active(void)
{
    return g_ms_size != 0;
}

unsigned nc_msearch_size(void)
{
    return g_ms_size;
}

uint32_t nc_msearch_count(void)
{
    return g_ms_size ? g_ms_count : 0;
}

uint32_t nc_msearch_list(uint32_t *va, uint32_t *now, uint32_t *prev, uint32_t max)
{
    NcVm *vm = nc_mods_vm();
    if (!vm || !g_ms_size || vm->ram_size != g_ms_ram) {
        return 0;
    }
    uint32_t k = 0;
    for (uint32_t pa = 0; pa + g_ms_size <= g_ms_ram && k < max; pa += g_ms_size) {
        if (!(g_cand[pa / 8] & (1u << (pa % 8)))) {
            continue;
        }
        const uint32_t v = vm->rmap[pa / NC_PAGE];
        va[k] = v ? (v & ~(NC_PAGE - 1)) | (pa & (NC_PAGE - 1)) : NC_CONTIG_WINDOW | pa;
        now[k] = val_at(vm->phys, pa, g_ms_size);
        prev[k] = val_at(g_prev, pa, g_ms_size);
        k++;
    }
    return k;
}
