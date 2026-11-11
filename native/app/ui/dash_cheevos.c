/*
 * xbased in-game menu — achievements (native/core/cheevos.c): the game's list
 * and the pop-up's settings; the memory search; what can be done with an
 * address found (watch it over the game, note what it is, make an
 * achievement of it); a new achievement put together without writing its
 * trigger; and the text entry (the on-screen keyboard) their names take.
 * docs/achievements.md walks through it.
 */
#include "native/app/ui/dash_int.h"
#include "native/app/ui/cheevo_art.h"
#include "native/core/cheevos.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* rcheevos' memory reference: 0xH 8-bit, 0x  16-bit, 0xX 32-bit */
static void memref(uint32_t a, unsigned size, char *out, size_t n)
{
    snprintf(out, n, size == 1 ? "0xH%08x" : size == 2 ? "0x %08x" : "0xX%08x", a);
}

static const char *bits(unsigned size)
{
    return size == 1 ? "8-bit" : size == 2 ? "16-bit" : "32-bit";
}

static unsigned next_size(unsigned size, int dir)
{
    return dir > 0 ? (size == 4 ? 1 : size * 2) : (size == 1 ? 4 : size / 2);
}

/* ------------------------------------------------------------ text entry */

static struct {
    char title[120];
    char buf[200];
    char *out;
    size_t cap;
    void (*done)(void);
} TX;

static void text_edit(const char *title, char *out, size_t cap, void (*done)(void))
{
    snprintf(TX.title, sizeof(TX.title), "%s", title);
    snprintf(TX.buf, sizeof(TX.buf), "%s", out);
    TX.out = out;
    TX.cap = cap < sizeof(TX.buf) ? cap : sizeof(TX.buf);
    TX.done = done;
    D.typing = true;
    SDL_StartTextInput(SDL_GetKeyboardFocus());
    dash_push(SCR_TEXT);
}

static void text_finish(bool keep)
{
    D.typing = false;
    SDL_StopTextInput(SDL_GetKeyboardFocus());
    dash_pop();
    if (keep) {
        snprintf(TX.out, TX.cap, "%s", TX.buf);
        if (TX.done) {
            TX.done();
        }
    }
}

void scr_text(void)
{
    const float W = ui_w(), H = ui_h();
    draw_header(TX.title, NULL);
    In in = D.in;
    if (in.text) {
        dash_text_add(TX.buf, TX.cap, in.text);
    }
    if (in.key == SDLK_BACKSPACE || in.key == SDLK_DELETE || in.x) {
        dash_text_rub(TX.buf);
    }
    if (in.y) {
        dash_text_add(TX.buf, TX.cap, " ");
    }
    const bool enter = in.key == SDLK_RETURN || in.key == SDLK_KP_ENTER;
    memset(&D.in, 0, sizeof(D.in));
    D.in.mx = D.in.my = -1;
    D.in.pad = in.pad;
    /* the field */
    const float fx = 120, fy = 150, fw = W - 240;
    ui_round(fx, fy, fw, 60, 12, (UiColor){0x0A, 0x1C, 0x06, 0xF0});
    ui_round_outline(fx, fy, fw, 60, 12, 2, UI_GREEN);
    char shown[260];
    snprintf(shown, sizeof(shown), "%s%s", TX.buf, (int)(ui_time() * 2) % 2 ? "_" : "");
    ui_text_fit(fx + 20, fy + 14, shown, 28, UI_FONT_REGULAR, UI_WHITE, UI_ALIGN_LEFT, fw - 40);
    /* the keyboard under it */
    const float ks = 52, kw = 10 * ks + 9 * 6;
    const bool done = dash_osk(TX.buf, TX.cap, (W - kw) / 2, fy + 100, ks, &in);
    ui_text(fx, H - 82, T(S_TEXT_HINT), 18, UI_FONT_REGULAR, UI_GREY, UI_ALIGN_LEFT);
    draw_hints(T(S_SELECT), T(S_BACK), T(S_DELETE), T(S_KB_SPACE));
    if (in.back) {
        text_finish(false);
    } else if (done || enter) {
        text_finish(true);
    }
}

/* ------------------------------------------------------------ settings and the list */

static const char *corner_name(int c)
{
    return T(c == 0 ? S_CORNER_TL : c == 1 ? S_CORNER_TR : c == 2 ? S_CORNER_BL : S_CORNER_BR);
}

static void relock(Dash *d)
{
    (void)d;
    nc_cheevos_relock_all();
}

static bool toggle_row(List *l, const char *label, const char *key, bool def)
{
    int d = 0;
    const bool on = cfg_bool(&D.global, key, def);
    if (list_row(l, label, T(on ? S_ON : S_OFF), &d) || d) {
        cfg_set_bool(&D.global, key, !on);
        dash_save_cfg();
        return true;
    }
    return false;
}

void scr_cheevos(void)
{
    const float W = ui_w(), H = ui_h();
    const unsigned n = nc_cheevos_count();
    unsigned got = 0, pts = 0, all_pts = 0;
    for (unsigned i = 0; i < n; i++) {
        const bool done = nc_cheevos_state(i) == 1;
        got += done;
        pts += done ? nc_cheevos_points(i) : 0;
        all_pts += nc_cheevos_state(i) >= 0 ? nc_cheevos_points(i) : 0;
    }
    char sub[96];
    snprintf(sub, sizeof(sub), T(S_CHEEVOS_COUNT), got, n);
    char sub2[140];
    snprintf(sub2, sizeof(sub2), "%s  ·  %u / %u %s", sub, pts, all_pts, T(S_CN_POINTS));
    draw_header(T(S_CHEEVOS), sub2);
    List l;
    list_begin(&l, 74, 150, W - 148, H - 244, 64);
    if (list_row(&l, T(S_MSEARCH), NULL, NULL)) {
        D.sel[SCR_MSEARCH] = 0;
        dash_push(SCR_MSEARCH);
    }
    char nw[16];
    snprintf(nw, sizeof(nw), "%u", nc_watch_count());
    if (list_row(&l, T(S_WATCHED), nw, NULL)) {
        D.sel[SCR_WATCH] = 0;
        dash_push(SCR_WATCH);
    }
    int d = 0;
    const int corner = cfg_int(&D.global, "cheevos.popup", 3) & 3;
    if (list_row(&l, T(S_CHEEVO_POPUP), corner_name(corner), &d) || d) {
        cfg_set_int(&D.global, "cheevos.popup", (corner + (d ? d : 1) + 4) % 4);
        dash_save_cfg();
    }
    if (list_row(&l, T(S_SNAP_MENU), NULL, NULL)) {
        nc_snapshot_request();      /* taken as the game runs again */
        dash_toast(T(S_SNAP_ON_RESUME));
    }
    toggle_row(&l, T(S_CHEEVO_SOUND), "cheevos.sound", true);
    toggle_row(&l, T(S_CHEEVO_SHOW_WATCH), "cheevos.watch", true);
    if (list_row(&l, T(S_CHEEVOS_RELOAD), NULL, NULL)) {
        nc_cheevos_reload();
        dash_toast(T(S_CHEEVOS_RELOADED));
    }
    for (unsigned i = 0; i < n; i++) {
        const int st = nc_cheevos_state(i);
        char sub_line[320], value[80];
        if (st < 0) {
            snprintf(sub_line, sizeof(sub_line), "%s  (%s)", nc_cheevos_desc(i), nc_cheevos_error(i));
        } else {
            snprintf(sub_line, sizeof(sub_line), "%s", nc_cheevos_desc(i));
        }
        snprintf(value, sizeof(value), "%s  ·  %u", st == 1 ? T(S_CHEEVO_DONE) : st == 0 ? T(S_CHEEVO_LOCKED)
                                                                                     : T(S_CHEEVO_NOT_SET),
                 nc_cheevos_points(i));
        list_row_icon(&l, cheevo_art(nc_cheevos_badge(i)), nc_cheevos_name(i), sub_line, value, NULL);
    }
    if (n && list_row(&l, T(S_CHEEVO_EXPORT), NULL, NULL)) {
        char path[1300], msg[1400];
        if (nc_cheevos_export(D.cur ? D.cur->g.title : "", path, sizeof(path))) {
            snprintf(msg, sizeof(msg), T(S_CHEEVO_EXPORTED), path);
            dash_toast(msg);
        }
    }
    if (n && list_row(&l, T(S_CHEEVOS_RELOCK), NULL, NULL)) {
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

/* ------------------------------------------------------------ an address: what to do with it */

static struct {
    uint32_t addr;
    unsigned size;
    char note[120];
} AD;

static void open_addr(uint32_t addr, unsigned size)
{
    AD.addr = addr;
    AD.size = size;
    snprintf(AD.note, sizeof(AD.note), "%s", nc_note_get(addr));
    D.sel[SCR_ADDR] = 0;
    dash_push(SCR_ADDR);
}

static void note_done(void)
{
    nc_note_set(AD.addr, AD.note);
}

static void new_from(uint32_t addr, unsigned size);

void scr_addr(void)
{
    const float W = ui_w(), H = ui_h();
    char ref[32], sub[200];
    memref(AD.addr, AD.size, ref, sizeof(ref));
    uint32_t v = 0;
    nc_cheevos_peek(AD.addr, AD.size, &v);
    snprintf(sub, sizeof(sub), "%s  ·  %s  ·  %u%s%s", ref, bits(AD.size), v, AD.note[0] ? "  ·  " : "", AD.note);
    draw_header(T(S_ADDRESS), sub);
    List l;
    list_begin(&l, 74, 150, W - 148, H - 244, 58);
    bool watched = false;
    unsigned wi = 0;
    for (unsigned i = 0; i < nc_watch_count(); i++) {
        uint32_t a;
        unsigned s;
        if (nc_watch_get(i, &a, &s) && a == AD.addr) {
            watched = true;
            wi = i;
        }
    }
    if (list_row(&l, T(S_MAKE_CHEEVO), NULL, NULL)) {
        new_from(AD.addr, AD.size);
    }
    if (list_row(&l, T(watched ? S_STOP_WATCH : S_WATCH_IT), NULL, NULL)) {
        if (watched) {
            nc_watch_remove(wi);
        } else if (!nc_watch_add(AD.addr, AD.size)) {
            dash_toast(T(S_WATCH_FULL));
        }
    }
    if (list_row(&l, T(S_ADD_NOTE), AD.note[0] ? AD.note : NULL, NULL)) {
        text_edit(T(S_ADD_NOTE), AD.note, sizeof(AD.note), note_done);
    }
    if (list_row(&l, T(S_COPY), ref, NULL)) {
        SDL_SetClipboardText(ref);
        char msg[120];
        snprintf(msg, sizeof(msg), T(S_COPIED), ref);
        dash_toast(msg);
    }
    list_end(&l);
    if (D.in.back) {
        dash_pop();
    }
    draw_hints(T(S_SELECT), T(S_BACK), NULL, NULL);
}

/* ------------------------------------------------------------ watched addresses */

void scr_watch(void)
{
    const float W = ui_w(), H = ui_h();
    draw_header(T(S_WATCHED), NULL);
    List l;
    list_begin(&l, 74, 150, W - 148, H - 244, 64);
    for (unsigned i = 0; i < nc_watch_count(); i++) {
        uint32_t a, v = 0;
        unsigned s;
        if (!nc_watch_get(i, &a, &s)) {
            continue;
        }
        char ref[32], val[64];
        memref(a, s, ref, sizeof(ref));
        nc_cheevos_peek(a, s, &v);
        snprintf(val, sizeof(val), "%u", v);
        const char *note = nc_note_get(a);
        if (list_row_icon(&l, NULL, note[0] ? note : ref, note[0] ? ref : bits(s), val, NULL)) {
            open_addr(a, s);
        }
    }
    list_end(&l);
    if (!nc_watch_count()) {
        ui_text(74, H - 82, T(S_WATCH_NONE), 18, UI_FONT_REGULAR, UI_GREY, UI_ALIGN_LEFT);
    }
    if (D.in.back) {
        dash_pop();
    }
    draw_hints(T(S_SELECT), T(S_BACK), NULL, NULL);
}

/* ------------------------------------------------------------ a new achievement */

enum { R_DOWN, R_UP, R_BECOMES, R_CHANGES, R_FROM_TO, R_COUNT };

static struct {
    uint32_t addr;
    unsigned size, points_i;
    int rule;
    long long a, b;
    char name[96], desc[200];
} NC;

static const unsigned k_points[] = {0, 1, 2, 3, 4, 5, 10, 25, 50, 100};

static void new_from(uint32_t addr, unsigned size)
{
    memset(&NC, 0, sizeof(NC));
    NC.addr = addr;
    NC.size = size;
    NC.points_i = 5;
    uint32_t v = 0;
    nc_cheevos_peek(addr, size, &v);
    NC.a = v;
    NC.b = v + 1;
    D.sel[SCR_CHEEVO_NEW] = 0;
    dash_push(SCR_CHEEVO_NEW);
}

static void build_trigger(char *out, size_t n)
{
    char p[32];
    memref(NC.addr, NC.size, p, sizeof(p));
    switch (NC.rule) {
    case R_DOWN: snprintf(out, n, "%s<d%s", p, p); break;
    case R_UP: snprintf(out, n, "%s>d%s", p, p); break;
    case R_BECOMES: snprintf(out, n, "d%s!=%lld_%s=%lld", p, NC.a, p, NC.a); break;
    case R_CHANGES: snprintf(out, n, "%s!=d%s", p, p); break;
    default: snprintf(out, n, "d%s=%lld_%s=%lld", p, NC.a, p, NC.b); break;
    }
}

static const char *rule_name(int r)
{
    return T(r == R_DOWN ? S_RULE_DOWN : r == R_UP ? S_RULE_UP : r == R_BECOMES ? S_RULE_BECOMES
             : r == R_CHANGES ? S_RULE_CHANGES : S_RULE_FROM_TO);
}

static void value_row(List *l, const char *label, long long *v)
{
    int d = 0;
    char s[40];
    snprintf(s, sizeof(s), "< %lld >", *v);
    if (list_row(l, label, s, &d) || d) {
        const long long max = NC.size == 4 ? 0xFFFFFFFFll : (1ll << (NC.size * 8)) - 1;
        *v += d * (D.in.lb || D.in.rb ? 100 : 1);
        *v = *v < 0 ? max : *v > max ? 0 : *v;
    }
}

static void create(void)
{
    char trig[200], err[120], msg[200];
    build_trigger(trig, sizeof(trig));
    if (!NC.name[0]) {
        dash_toast(T(S_CN_NEED_NAME));
        text_edit(T(S_CN_NAME), NC.name, sizeof(NC.name), NULL);
        return;
    }
    if (nc_cheevos_add(NC.name, NC.desc, trig, k_points[NC.points_i], err, sizeof(err))) {
        snprintf(msg, sizeof(msg), T(S_CN_CREATED), NC.name);
        dash_toast(msg);
        /* back to the list, where it now is */
        while (D.sp > 0 && dash_top() != SCR_CHEEVOS) {
            dash_pop();
        }
    } else {
        snprintf(msg, sizeof(msg), "%s: %s", T(S_CHEEVO_NOT_SET), err);
        dash_toast(msg);
    }
}

void scr_cheevo_new(void)
{
    const float W = ui_w(), H = ui_h();
    draw_header(T(S_NEW_CHEEVO), NULL);
    char trig[200], ref[32];
    build_trigger(trig, sizeof(trig));
    memref(NC.addr, NC.size, ref, sizeof(ref));
    List l;
    list_begin(&l, 74, 150, W - 148, H - 244, 58);
    /* how it will look */
    char pts[24];
    snprintf(pts, sizeof(pts), "%u", k_points[NC.points_i]);
    list_row_icon(&l, cheevo_art(NULL), NC.name[0] ? NC.name : T(S_CN_NAME), NC.desc[0] ? NC.desc : T(S_CN_DESC),
                  pts, NULL);
    if (list_row(&l, T(S_CN_NAME), NC.name, NULL)) {
        text_edit(T(S_CN_NAME), NC.name, sizeof(NC.name), NULL);
    }
    if (list_row(&l, T(S_CN_DESC), NC.desc, NULL)) {
        text_edit(T(S_CN_DESC), NC.desc, sizeof(NC.desc), NULL);
    }
    char addr[200];
    const char *note = nc_note_get(NC.addr);
    snprintf(addr, sizeof(addr), "%s%s%s", ref, note[0] ? "  " : "", note);
    list_row(&l, T(S_ADDRESS), addr, NULL);
    int d = 0;
    if (list_row(&l, T(S_MSEARCH_SIZE), bits(NC.size), &d) || d) {
        NC.size = next_size(NC.size, d ? d : 1);
    }
    d = 0;
    if (list_row(&l, T(S_CN_RULE), rule_name(NC.rule), &d) || d) {
        NC.rule = (NC.rule + (d ? d : 1) + R_COUNT) % R_COUNT;
    }
    if (NC.rule == R_BECOMES) {
        value_row(&l, T(S_CN_VALUE), &NC.a);
    } else if (NC.rule == R_FROM_TO) {
        value_row(&l, T(S_CN_FROM), &NC.a);
        value_row(&l, T(S_CN_TO), &NC.b);
    }
    d = 0;
    if (list_row(&l, T(S_CN_POINTS), pts, &d) || d) {
        const int np = (int)(sizeof(k_points) / sizeof(k_points[0]));
        NC.points_i = (unsigned)(((int)NC.points_i + (d ? d : 1) + np) % np);
    }
    if (list_row(&l, T(S_CN_CREATE), NULL, NULL)) {
        create();
    }
    list_end(&l);
    char foot[260];
    snprintf(foot, sizeof(foot), "%s: %s", T(S_CN_TRIGGER), trig);
    ui_text(74, H - 82, foot, 18, UI_FONT_REGULAR, UI_GREY, UI_ALIGN_LEFT);
    if (D.in.back) {
        dash_pop();
    }
    draw_hints(T(S_SELECT), T(S_BACK), NULL, NULL);
}

/* ------------------------------------------------------------ the memory search */

#define SHOWN 40

static unsigned g_size = 1;         /* bytes: 1, 2, 4 */
static long long g_value;           /* the "equal to" value */
static uint32_t g_addr[SHOWN], g_now[SHOWN], g_prev[SHOWN], g_shown;
static bool g_stale = true;

/* the in-game menu opened: the game has run since the list was read */
void dash_cheevos_opened(void)
{
    g_stale = true;
}

static void refresh(void)
{
    g_shown = nc_msearch_list(g_addr, g_now, g_prev, SHOWN);
    g_stale = false;
}

static void filter(int op)
{
    if (!nc_msearch_active()) {
        nc_msearch_new(g_size);
    }
    nc_msearch_filter(op, (uint32_t)g_value);
    refresh();
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
    int d = 0;
    if (list_row(&l, T(S_MSEARCH_SIZE), bits(g_size), &d) || d) {
        g_size = next_size(g_size, d ? d : 1);
    }
    if (list_row(&l, T(S_MSEARCH_NEW), NULL, NULL)) {
        nc_msearch_new(g_size);
        refresh();
        dash_toast(T(S_MSEARCH_STARTED));
    }
    d = 0;
    char v[64];
    snprintf(v, sizeof(v), "< %lld >", g_value);
    if (list_row(&l, T(S_MSEARCH_VALUE), v, &d) || d) {
        const long long max = g_size == 4 ? 0xFFFFFFFFll : (1ll << (g_size * 8)) - 1;
        g_value += d * (D.in.lb || D.in.rb ? 100 : 1);
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
            char ref[32], val[180];
            memref(g_addr[i], nc_msearch_size(), ref, sizeof(ref));
            const char *note = nc_note_get(g_addr[i]);
            snprintf(val, sizeof(val), "%u  (%u)%s%s", g_now[i], g_prev[i], note[0] ? "  " : "", note);
            if (list_row(&l, ref, val, NULL)) {
                open_addr(g_addr[i], nc_msearch_size());
            }
        }
    }
    list_end(&l);
    const char *foot = !nc_msearch_active() ? T(S_MSEARCH_HOW)
                       : nc_msearch_count() > 2000 ? T(S_MSEARCH_NARROW)
                                                   : T(S_MSEARCH_PICK);
    ui_text(74, H - 82, foot, 18, UI_FONT_REGULAR, UI_GREY, UI_ALIGN_LEFT);
    if (D.in.back) {
        g_stale = true;
        dash_pop();
    }
    draw_hints(T(S_SELECT), T(S_BACK), NULL, NULL);
}
