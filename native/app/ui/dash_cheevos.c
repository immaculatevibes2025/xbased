/*
 * xbased in-game menu — achievements (native/core/cheevos.c): the game's
 * list, unlocked or not; and the memory search for finding the addresses
 * new ones are written with (docs/achievements.md walks through it).
 */
#include "native/app/ui/dash_int.h"
#include "native/core/cheevos.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------ the list */

static void relock(Dash *d)
{
    (void)d;
    nc_cheevos_relock_all();
}

void scr_cheevos(void)
{
    const float W = ui_w(), H = ui_h();
    const unsigned n = nc_cheevos_count();
    unsigned got = 0;
    for (unsigned i = 0; i < n; i++) {
        got += nc_cheevos_state(i) == 1;
    }
    char sub[64];
    snprintf(sub, sizeof(sub), T(S_CHEEVOS_COUNT), got, n);
    draw_header(T(S_CHEEVOS), sub);
    List l;
    list_begin(&l, 74, 150, W - 148, H - 244, 64);
    if (list_row(&l, T(S_MSEARCH), NULL, NULL)) {
        D.sel[SCR_MSEARCH] = 0;
        dash_push(SCR_MSEARCH);
    }
    if (list_row(&l, T(S_CHEEVOS_RELOAD), NULL, NULL)) {
        nc_cheevos_reload();
        dash_toast(T(S_CHEEVOS_RELOADED));
    }
    for (unsigned i = 0; i < n; i++) {
        const int st = nc_cheevos_state(i);
        const char *desc = nc_cheevos_desc(i);
        char line[300];
        if (st < 0) {
            snprintf(line, sizeof(line), "%s  (%s)", desc, nc_cheevos_error(i));
            desc = line;
        }
        list_row_icon(&l, NULL, nc_cheevos_name(i), desc,
                      st == 1 ? T(S_CHEEVO_DONE) : st == 0 ? T(S_CHEEVO_LOCKED) : T(S_CHEEVO_NOT_SET), NULL);
    }
    if (list_row(&l, T(S_CHEEVOS_RELOCK), NULL, NULL) && n) {
        dash_ask(T(S_CHEEVOS_RELOCK_ASK), relock);
    }
    list_end(&l);
    if (!n) {
        ui_text(74, H - 82, T(S_CHEEVOS_NONE), 18, UI_FONT_REGULAR, UI_GREY, UI_ALIGN_LEFT);
    }
    if (D.in.back) {
        dash_pop();
    }
    draw_hints(T(S_SELECT), T(S_BACK), NULL, NULL);
}

/* ------------------------------------------------------------ the memory search */

#define SHOWN 40

static unsigned g_size = 1;         /* bytes: 1, 2, 4 */
static long long g_value;           /* the "equal to" value */
static uint32_t g_va[SHOWN], g_now[SHOWN], g_prev[SHOWN], g_shown;
static bool g_stale = true;

/* the in-game menu opened: the game has run since the list was read */
void dash_cheevos_opened(void)
{
    g_stale = true;
}

static void refresh(void)
{
    g_shown = nc_msearch_list(g_va, g_now, g_prev, SHOWN);
    g_stale = false;
}

/* rcheevos' memory reference: 0xH 8-bit, 0x  16-bit, 0xX 32-bit */
static void memref(uint32_t va, unsigned size, char *out, size_t n)
{
    snprintf(out, n, size == 1 ? "0xH%08x" : size == 2 ? "0x %08x" : "0xX%08x", va);
}

static void filter(int op)
{
    if (!nc_msearch_active()) {
        nc_msearch_new(g_size);
    }
    nc_msearch_filter(op, (uint32_t)g_value);
    refresh();
}

/* a found address kept: the clipboard, and found.txt beside the achievements */
static void keep(unsigned i)
{
    char ref[32], msg[200], p[1200];
    memref(g_va[i], nc_msearch_size(), ref, sizeof(ref));
    SDL_SetClipboardText(ref);
    snprintf(p, sizeof(p), "%sachievements", cfg_data_dir());
    SDL_CreateDirectory(p);
    snprintf(p, sizeof(p), "%sachievements/found.txt", cfg_data_dir());
    FILE *f = fopen(p, "a");
    if (f) {
        fprintf(f, "%s %s  now %u, before %u\n", D.cur->g.title, ref, g_now[i], g_prev[i]);
        fclose(f);
    }
    snprintf(msg, sizeof(msg), T(S_MSEARCH_KEPT), ref);
    dash_toast(msg);
}

void scr_msearch(void)
{
    const float W = ui_w(), H = ui_h();
    if (g_stale && nc_msearch_active()) {
        refresh();
    }
    char sub[80] = "";
    if (nc_msearch_active()) {
        snprintf(sub, sizeof(sub), T(S_MSEARCH_LEFT), nc_msearch_count());
    }
    draw_header(T(S_MSEARCH), sub[0] ? sub : NULL);
    List l;
    list_begin(&l, 74, 150, W - 148, H - 244, 52);
    char v[64];
    int d = 0;
    snprintf(v, sizeof(v), "%u-bit", g_size * 8);
    if (list_row(&l, T(S_MSEARCH_SIZE), v, &d) || d) {
        const int dir = d ? d : 1;
        g_size = dir > 0 ? (g_size == 4 ? 1 : g_size * 2) : (g_size == 1 ? 4 : g_size / 2);
    }
    if (list_row(&l, T(S_MSEARCH_NEW), NULL, NULL)) {
        nc_msearch_new(g_size);
        refresh();
        dash_toast(T(S_MSEARCH_STARTED));
    }
    d = 0;
    snprintf(v, sizeof(v), "< %lld >", g_value);
    if (list_row(&l, T(S_MSEARCH_VALUE), v, &d) || d) {
        const long long max = g_size == 4 ? 0xFFFFFFFFll : (1ll << (g_size * 8)) - 1;
        const int step = D.in.lb || D.in.rb ? 100 : 1;
        g_value += d * step;
        g_value = g_value < 0 ? max : g_value > max ? 0 : g_value;
    }
    if (list_row(&l, T(S_MSEARCH_EQUAL), NULL, NULL)) {
        filter(NC_MS_EQUAL);
    }
    if (list_row(&l, T(S_MSEARCH_CHANGED), NULL, NULL)) {
        filter(NC_MS_CHANGED);
    }
    if (list_row(&l, T(S_MSEARCH_UNCHANGED), NULL, NULL)) {
        filter(NC_MS_UNCHANGED);
    }
    if (list_row(&l, T(S_MSEARCH_INCREASED), NULL, NULL)) {
        filter(NC_MS_INCREASED);
    }
    if (list_row(&l, T(S_MSEARCH_DECREASED), NULL, NULL)) {
        filter(NC_MS_DECREASED);
    }
    if (nc_msearch_active() && nc_msearch_count() <= 2000) {
        for (unsigned i = 0; i < g_shown; i++) {
            char ref[32], val[64];
            memref(g_va[i], nc_msearch_size(), ref, sizeof(ref));
            snprintf(val, sizeof(val), "%u  (%u)", g_now[i], g_prev[i]);
            if (list_row(&l, ref, val, NULL)) {
                keep(i);
            }
        }
    }
    list_end(&l);
    const char *note = !nc_msearch_active() ? T(S_MSEARCH_HOW)
                       : nc_msearch_count() > 2000 ? T(S_MSEARCH_NARROW)
                                                   : T(S_MSEARCH_PICK);
    ui_text(74, H - 82, note, 18, UI_FONT_REGULAR, UI_GREY, UI_ALIGN_LEFT);
    if (D.in.back) {
        g_stale = true;     /* the game will have run when this opens again */
        dash_pop();
    }
    draw_hints(T(S_SELECT), T(S_BACK), NULL, NULL);
}
