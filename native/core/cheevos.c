/*
 * xbased native core — achievements (cheevos.h): local achievement files run
 * by rcheevos' runtime, address notes and watches, and the memory search that
 * finds the addresses.
 */
#include "native/core/cheevos.h"
#include "native/core/vm.h"
#include "native/core/nc.h"

#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifdef NC_HAVE_RCHEEVOS
#include "rc_runtime.h"
#include "rc_error.h"
#include "rc_runtime_types.h"
#endif

/* mods.c: the game's memory once bound */
NcVm *nc_mods_vm(void);

#define TAG "xbased-cheevos"

typedef struct {
    char name[96];
    char desc[200];
    char trigger[1024];
    char badge[64];
    char error[96];
    unsigned points;
    int state;              /* 1 unlocked, 0 locked, -1 no working trigger */
} Cheevo;

typedef struct {
    uint32_t addr;
    char text[120];
} Note;

static char g_exe_dir[1024], g_data_dir[1024];
static uint32_t g_title;
static Cheevo g_c[NC_CHEEVOS_MAX];
static unsigned g_n;
static Note g_notes[256];
static unsigned g_nnotes;
static uint32_t g_watch_addr[NC_WATCH_MAX];
static unsigned g_watch_size[NC_WATCH_MAX], g_nwatch;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned g_pending[8];   /* unlocks for the app's pop-up */
static unsigned g_pend_head, g_pend_tail;
#ifdef NC_HAVE_RCHEEVOS
static rc_runtime_t g_rt;
static bool g_rt_up;
#endif

void nc_cheevos_set_dirs(const char *exe_dir, const char *data_dir)
{
    snprintf(g_exe_dir, sizeof(g_exe_dir), "%s", exe_dir ? exe_dir : "");
    snprintf(g_data_dir, sizeof(g_data_dir), "%s", data_dir ? data_dir : "");
}

uint32_t nc_cheevos_title(void)
{
    return g_title;
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

static void make_dir(void)
{
    char p[1200];
    snprintf(p, sizeof(p), "%sachievements", g_data_dir);
    mkdir(p, 0775);     /* the platform's own on Windows (the build maps it) */
}

static void data_path(const char *ext, char *out, size_t n)
{
    snprintf(out, n, "%sachievements/%08X%s", g_data_dir, g_title, ext);
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

static Cheevo *slot_for(const char *name)
{
    Cheevo *c = by_name(name);
    if (!c && g_n < NC_CHEEVOS_MAX) {
        c = &g_c[g_n++];
    }
    if (c) {
        memset(c, 0, sizeof(*c));
        snprintf(c->name, sizeof(c->name), "%s", name);
        c->points = 5;
    }
    return c;
}

/* xbased's format: [name], desc:, trigger:, points:, badge: */
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
            c = slot_for(name);
            got += c != NULL;
        } else if (c && !strncmp(line, "desc:", 5)) {
            snprintf(c->desc, sizeof(c->desc), "%s", line + 5);
            trim(c->desc);
        } else if (c && !strncmp(line, "trigger:", 8)) {
            snprintf(c->trigger, sizeof(c->trigger), "%s", line + 8);
            trim(c->trigger);
        } else if (c && !strncmp(line, "points:", 7)) {
            c->points = (unsigned)atoi(line + 7);
        } else if (c && !strncmp(line, "badge:", 6)) {
            snprintf(c->badge, sizeof(c->badge), "%s", line + 6);
            trim(c->badge);
        }
    }
    fclose(f);
    if (got) {
        nc_log(TAG, "%s: %u achievements", path, got);
    }
}

/* a quoted field of RetroAchievements' format ("\"" an escaped quote): past it */
static const char *quoted(const char *p, char *out, size_t n)
{
    size_t o = 0;
    if (*p != '"') {
        return NULL;
    }
    for (p++; *p && *p != '"'; p++) {
        if (*p == '\\' && p[1]) {
            p++;
        }
        if (o + 1 < n) {
            out[o++] = *p;
        }
    }
    out[o] = 0;
    return *p == '"' ? p + 1 : NULL;
}

/* RetroAchievements' local achievements (their toolkit's <game>-User.txt):
 * a version line, the game's title, then a line an achievement:
 *   ID:"trigger":"title":"description":...:Author:Points:...:Badge
 * points the sixth field from the end, the badge the last (old and new
 * versions of the format alike). */
static void read_ra(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        return;
    }
    char line[2048];
    unsigned got = 0;
    while (fgets(line, sizeof(line), f)) {
        trim(line);
        char *colon = strchr(line, ':');
        if (!colon || !isdigit((unsigned char)line[0]) || colon[1] != '"') {
            continue;
        }
        char trig[1024], title[96], desc[200];
        const char *p = quoted(colon + 1, trig, sizeof(trig));
        p = p && *p == ':' ? quoted(p + 1, title, sizeof(title)) : NULL;
        p = p && *p == ':' ? quoted(p + 1, desc, sizeof(desc)) : NULL;
        if (!p || !title[0]) {
            continue;
        }
        const char *fields[24];
        int nf = 0;
        for (const char *q = p; *q == ':' && nf < 24; q = strchr(q + 1, ':') ? strchr(q + 1, ':') : "") {
            fields[nf++] = q + 1;
        }
        Cheevo *c = slot_for(title);
        if (!c) {
            break;
        }
        snprintf(c->trigger, sizeof(c->trigger), "%s", trig);
        snprintf(c->desc, sizeof(c->desc), "%s", desc);
        if (nf >= 6) {
            c->points = (unsigned)atoi(fields[nf - 6]);
            snprintf(c->badge, sizeof(c->badge), "%s", fields[nf - 1]);
            trim(c->badge);
            if (!strcmp(c->badge, "00000")) {
                c->badge[0] = 0;
            }
        }
        got++;
    }
    fclose(f);
    if (got) {
        nc_log(TAG, "%s: %u achievements (RetroAchievements format)", path, got);
    }
}

static void read_unlocked(void)
{
    char p[1200], line[200];
    data_path(".unlocked", p, sizeof(p));
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
    make_dir();
    data_path(".unlocked", p, sizeof(p));
    FILE *f = fopen(p, "a");
    if (f) {
        fprintf(f, "%s\n", name);
        fclose(f);
    }
}

/* ------------------------------------------------------------ notes and watches */

static void read_notes(void)
{
    g_nnotes = 0;
    char p[1200], line[300];
    data_path(".notes", p, sizeof(p));
    FILE *f = fopen(p, "r");
    while (f && fgets(line, sizeof(line), f) && g_nnotes < 256) {
        trim(line);
        char *sp = strchr(line, ' ');
        if (!sp || strncmp(line, "0x", 2)) {
            continue;
        }
        *sp = 0;
        g_notes[g_nnotes].addr = (uint32_t)strtoul(line, NULL, 16);
        snprintf(g_notes[g_nnotes].text, sizeof(g_notes[0].text), "%s", sp + 1);
        trim(g_notes[g_nnotes].text);
        g_nnotes++;
    }
    if (f) {
        fclose(f);
    }
}

static void save_notes(void)
{
    char p[1200];
    make_dir();
    data_path(".notes", p, sizeof(p));
    FILE *f = fopen(p, "w");
    for (unsigned i = 0; f && i < g_nnotes; i++) {
        fprintf(f, "0x%08x %s\n", g_notes[i].addr, g_notes[i].text);
    }
    if (f) {
        fclose(f);
    }
}

const char *nc_note_get(uint32_t address)
{
    for (unsigned i = 0; i < g_nnotes; i++) {
        if (g_notes[i].addr == address) {
            return g_notes[i].text;
        }
    }
    return "";
}

void nc_note_set(uint32_t address, const char *text)
{
    unsigned i = 0;
    while (i < g_nnotes && g_notes[i].addr != address) {
        i++;
    }
    if (!text || !text[0]) {
        if (i < g_nnotes) {
            g_notes[i] = g_notes[--g_nnotes];
        }
    } else if (i < 256) {
        g_notes[i].addr = address;
        snprintf(g_notes[i].text, sizeof(g_notes[0].text), "%s", text);
        g_nnotes += i == g_nnotes;
    }
    save_notes();
}

static void read_watch(void)
{
    g_nwatch = 0;
    char p[1200], line[100];
    data_path(".watch", p, sizeof(p));
    FILE *f = fopen(p, "r");
    unsigned sz;
    unsigned long a;
    while (f && fgets(line, sizeof(line), f) && g_nwatch < NC_WATCH_MAX) {
        if (sscanf(line, "%lx %u", &a, &sz) == 2 && (sz == 1 || sz == 2 || sz == 4)) {
            g_watch_addr[g_nwatch] = (uint32_t)a;
            g_watch_size[g_nwatch++] = sz;
        }
    }
    if (f) {
        fclose(f);
    }
}

static void save_watch(void)
{
    char p[1200];
    make_dir();
    data_path(".watch", p, sizeof(p));
    FILE *f = fopen(p, "w");
    for (unsigned i = 0; f && i < g_nwatch; i++) {
        fprintf(f, "0x%08x %u\n", g_watch_addr[i], g_watch_size[i]);
    }
    if (f) {
        fclose(f);
    }
}

bool nc_watch_add(uint32_t address, unsigned size)
{
    for (unsigned i = 0; i < g_nwatch; i++) {
        if (g_watch_addr[i] == address) {
            g_watch_size[i] = size;
            save_watch();
            return true;
        }
    }
    if (g_nwatch >= NC_WATCH_MAX) {
        return false;
    }
    g_watch_addr[g_nwatch] = address;
    g_watch_size[g_nwatch++] = size;
    save_watch();
    return true;
}

void nc_watch_remove(unsigned i)
{
    if (i < g_nwatch) {
        memmove(&g_watch_addr[i], &g_watch_addr[i + 1], (g_nwatch - i - 1) * sizeof(g_watch_addr[0]));
        memmove(&g_watch_size[i], &g_watch_size[i + 1], (g_nwatch - i - 1) * sizeof(g_watch_size[0]));
        g_nwatch--;
        save_watch();
    }
}

unsigned nc_watch_count(void)
{
    return g_nwatch;
}

bool nc_watch_get(unsigned i, uint32_t *address, unsigned *size)
{
    if (i >= g_nwatch) {
        return false;
    }
    *address = g_watch_addr[i];
    *size = g_watch_size[i];
    return true;
}

/* ------------------------------------------------------------ memory */

static NcCheevosRead g_read;

/* RetroAchievements' Xbox address: physical RAM; 64 MB and up the game's virtual */
static bool read_bytes(uint32_t address, uint8_t *b, unsigned n)
{
    NcVm *vm = nc_mods_vm();
    if (vm && address < vm->ram_size) {
        for (unsigned i = 0; i < n; i++) {
            b[i] = address + i < vm->ram_size ? vm->phys[address + i] : 0;
        }
        return true;
    }
    return g_read && g_read(address, b, n);
}

bool nc_cheevos_peek(uint32_t address, unsigned size, uint32_t *value)
{
    uint8_t b[4] = {0};
    if (size > 4 || !read_bytes(address, b, size)) {
        return false;
    }
    *value = (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
    return true;
}

/* ------------------------------------------------------------ snapshots */

static volatile int g_snap_req, g_snap_done;
static unsigned g_frame;

void nc_snapshot_request(void)
{
    g_snap_req = 1;
}

int nc_snapshot_take_done(void)
{
    const int d = g_snap_done;
    g_snap_done = 0;
    return d;
}

static bool write_file(const char *path, const void *data, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        return false;
    }
    const bool ok = fwrite(data, 1, n, f) == n;
    return fclose(f) == 0 && ok;
}

/* the guest stopped: RAM, its page map and a note, numbered after the last */
static void take_snapshot(void)
{
    NcVm *vm = nc_mods_vm();
    if (!vm || !g_title) {
        g_snap_done = -1;
        return;
    }
    char dir[1200], p[1300];
    make_dir();
    snprintf(dir, sizeof(dir), "%sachievements/snapshots", g_data_dir);
    mkdir(dir, 0775);
    int n = 1;
    for (;; n++) {
        snprintf(p, sizeof(p), "%s/%08X-%d.ram", dir, g_title, n);
        FILE *f = fopen(p, "rb");
        if (!f) {
            break;
        }
        fclose(f);
    }
    bool ok = write_file(p, vm->phys, vm->ram_size);
    snprintf(p, sizeof(p), "%s/%08X-%d.map", dir, g_title, n);
    ok = ok && write_file(p, vm->rmap, (vm->ram_size / NC_PAGE) * sizeof(uint32_t));
    snprintf(p, sizeof(p), "%s/%08X-%d.txt", dir, g_title, n);
    FILE *f = fopen(p, "w");
    if (f) {
        time_t t = time(NULL);
        char when[64];
        strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", localtime(&t));
        fprintf(f, "title %08X\nsnapshot %d\ntaken %s\nframe %u\nram %u\n", g_title, n, when, g_frame,
                vm->ram_size);
        fclose(f);
    }
    nc_log(TAG, "snapshot %d: %s", n, ok ? "written" : "failed");
    g_snap_done = ok ? n : -1;
}

/* ------------------------------------------------------------ the runtime */

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
    data_path("-User.txt", p, sizeof(p));
    read_ra(p);
#ifdef NC_HAVE_RCHEEVOS
    if (g_rt_up) {
        rc_runtime_destroy(&g_rt);
    }
    rc_runtime_init(&g_rt);
    g_rt_up = true;
#endif
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
    unsigned live = 0;
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
    read_notes();
    read_watch();
    pthread_mutex_unlock(&g_lock);
}

/* at once (the in-game menu has the game paused: no frame would run it) */
void nc_cheevos_reload(void)
{
    pthread_mutex_lock(&g_lock);
    if (g_title) {
        load_all();
    }
    pthread_mutex_unlock(&g_lock);
}

void nc_cheevos_relock_all(void)
{
    pthread_mutex_lock(&g_lock);
    char p[1200];
    data_path(".unlocked", p, sizeof(p));
    remove(p);
    if (g_title) {
        load_all();
    }
    pthread_mutex_unlock(&g_lock);
}

bool nc_cheevos_add(const char *name, const char *desc, const char *trigger, unsigned points, char *err, size_t en)
{
    err[0] = 0;
    if (!name[0] || !trigger[0]) {
        snprintf(err, en, "a name and a trigger are needed");
        return false;
    }
#ifdef NC_HAVE_RCHEEVOS
    const int sz = rc_trigger_size(trigger);
    if (sz < 0) {
        snprintf(err, en, "%s", rc_error_str(sz));
        return false;
    }
#endif
    make_dir();
    char p[1200];
    snprintf(p, sizeof(p), "%sachievements/%08X.txt", g_data_dir, g_title);
    FILE *f = fopen(p, "a");
    if (!f) {
        snprintf(err, en, "%s couldn't be written", p);
        return false;
    }
    fprintf(f, "\n[%s]\ndesc: %s\npoints: %u\ntrigger: %s\n", name, desc, points, trigger);
    fclose(f);
    nc_log(TAG, "added %s: %s", name, trigger);
    nc_cheevos_reload();
    return true;
}

static void put_quoted(FILE *f, const char *s)
{
    fputc('"', f);
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') {
            fputc('\\', f);
        }
        fputc(*s, f);
    }
    fputc('"', f);
}

bool nc_cheevos_export(const char *game_title, char *out, size_t n)
{
    make_dir();
    data_path("-User.txt", out, n);
    char tmp[1300];
    snprintf(tmp, sizeof(tmp), "%s.tmp", out);
    FILE *f = fopen(tmp, "w");
    if (!f) {
        return false;
    }
    pthread_mutex_lock(&g_lock);
    fprintf(f, "1.0\n%s\n", game_title ? game_title : "");
    for (unsigned i = 0; i < g_n; i++) {
        const Cheevo *c = &g_c[i];
        if (!c->trigger[0]) {
            continue;
        }
        /* local IDs as RetroAchievements' toolkit numbers them */
        fprintf(f, "%u:", 111000001u + i);
        put_quoted(f, c->trigger);
        fputc(':', f);
        put_quoted(f, c->name);
        fputc(':', f);
        put_quoted(f, c->desc);
        fprintf(f, "::::xbased:%u:::::%s\n", c->points, c->badge[0] ? c->badge : "00000");
    }
    pthread_mutex_unlock(&g_lock);
    fclose(f);
    remove(out);
    return rename(tmp, out) == 0;
}

#ifdef NC_HAVE_RCHEEVOS
static uint32_t RC_CCONV peek(uint32_t address, uint32_t num_bytes, void *ud)
{
    (void)ud;
    uint32_t v = 0;
    return nc_cheevos_peek(address, num_bytes, &v) ? v : 0;
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
        g_pending[g_pend_head % 8] = e->id - 1;
        g_pend_head++;
    }
}
#endif

void nc_cheevos_frame(NcCheevosRead read)
{
    g_read = read;
    g_frame++;
    if (g_snap_req) {
        g_snap_req = 0;
        take_snapshot();
    }
    pthread_mutex_lock(&g_lock);
#ifdef NC_HAVE_RCHEEVOS
    if (g_rt_up && g_n) {
        rc_runtime_do_frame(&g_rt, on_event, peek, NULL, NULL);
    }
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

const char *nc_cheevos_badge(unsigned i)
{
    return i < g_n ? g_c[i].badge : "";
}

unsigned nc_cheevos_points(unsigned i)
{
    return i < g_n ? g_c[i].points : 0;
}

int nc_cheevos_state(unsigned i)
{
    return i < g_n ? g_c[i].state : -1;
}

const char *nc_cheevos_error(unsigned i)
{
    return i < g_n ? g_c[i].error : "";
}

bool nc_cheevos_take_unlock(unsigned *index)
{
    pthread_mutex_lock(&g_lock);
    const bool any = g_pend_tail != g_pend_head;
    if (any) {
        *index = g_pending[g_pend_tail % 8];
        g_pend_tail++;
    }
    pthread_mutex_unlock(&g_lock);
    return any;
}

/* ------------------------------------------------------------ memory search */

static uint8_t *g_prev;         /* RAM at the last filter */
static uint8_t *g_cand;         /* a bit an address: still a candidate */
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

uint32_t nc_msearch_list(uint32_t *address, uint32_t *now, uint32_t *prev, uint32_t max)
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
        address[k] = pa;
        now[k] = val_at(vm->phys, pa, g_ms_size);
        prev[k] = val_at(g_prev, pa, g_ms_size);
        k++;
    }
    return k;
}
