/*
 * xbased front end — a game's title updates and downloadable content
 * (E:\TDATA\<title ID>\$u, $c\<offer>): those installed listed and
 * removable, more installed from a .zip the user has, or found online on
 * xbmc4gamers.co.uk's URL Downloader (its DLC section's zips, named with the
 * title ID) and installed from there. A zip's TDATA folder is found however
 * it was packed (TDATA\<title ID>\..., <title ID>\..., $c and $u at its top,
 * an offer's folder alone, an update's default.xbe alone) and copied over
 * the hard disk's. Signed for this console when the game starts
 * (xi_resign_downloads), as the copied-in-by-hand ones always were.
 */
#include "native/app/ui/dash_int.h"
#include "native/core/nc.h"

#include <SDL3/SDL.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* raw deflate: a zip's entries */
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"

#define DL_PAGE "https://www.xbmc4gamers.co.uk/xbox/urldownloader/"
#define MAX_ITEMS 64
#define MAX_ZIP (512u << 20)

/* ------------------------------------------------------------ files */

static void hdd_dir(char *out, size_t n)
{
    const char *h = cfg_get(&D.global, "hdd");
    if (h && *h) {
        snprintf(out, n, "%s", h);
    } else {
        snprintf(out, n, "%shdd", cfg_data_dir());
    }
}

/* E:\TDATA\<title ID> on the PC: the folder there in whatever case it has, else lowercase */
static void title_dir(uint32_t tid, char *out, size_t n)
{
    char hdd[1024], tdata[1100], want[16];
    hdd_dir(hdd, sizeof(hdd));
    snprintf(tdata, sizeof(tdata), "%s/Partition1/TDATA", hdd);
    snprintf(want, sizeof(want), "%08x", tid);
    snprintf(out, n, "%s/%s", tdata, want);
    int c = 0;
    char **names = SDL_GlobDirectory(tdata, NULL, 0, &c);
    for (int i = 0; names && i < c; i++) {
        if (!SDL_strcasecmp(names[i], want)) {
            snprintf(out, n, "%s/%s", tdata, names[i]);
        }
    }
    SDL_free(names);
}

static bool is_dir(const char *p)
{
    SDL_PathInfo pi;
    return SDL_GetPathInfo(p, &pi) && pi.type == SDL_PATHTYPE_DIRECTORY;
}

static void remove_tree(const char *dir)
{
    int n = 0;
    char **names = SDL_GlobDirectory(dir, NULL, 0, &n);
    for (int i = 0; names && i < n; i++) {
        char p[1400];
        snprintf(p, sizeof(p), "%s/%s", dir, names[i]);
        if (is_dir(p)) {
            remove_tree(p);
        } else {
            SDL_RemovePath(p);
        }
    }
    SDL_free(names);
    SDL_RemovePath(dir);
}

/* from's files copied into to (folders merged, files replaced); the files copied */
static int copy_tree(const char *from, const char *to)
{
    SDL_CreateDirectory(to);
    int n = 0, done = 0;
    char **names = SDL_GlobDirectory(from, NULL, 0, &n);
    for (int i = 0; names && i < n; i++) {
        char s[1400], d[1400];
        snprintf(s, sizeof(s), "%s/%s", from, names[i]);
        /* an existing folder of another case: the same one on the Xbox's FATX */
        char name[300];
        snprintf(name, sizeof(name), "%s", names[i]);
        int m = 0;
        char **have = SDL_GlobDirectory(to, NULL, 0, &m);
        for (int k = 0; have && k < m; k++) {
            if (!SDL_strcasecmp(have[k], names[i])) {
                snprintf(name, sizeof(name), "%s", have[k]);
            }
        }
        SDL_free(have);
        snprintf(d, sizeof(d), "%s/%s", to, name);
        if (is_dir(s)) {
            done += copy_tree(s, d);
        } else if (SDL_CopyFile(s, d)) {
            done++;
        } else {
            nc_log("xbased-content", "%s couldn't be written: %s", d, SDL_GetError());
        }
    }
    SDL_free(names);
    return done;
}

/* ------------------------------------------------------------ zips */

static uint32_t le16(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8;
}

static uint32_t le32(const uint8_t *p)
{
    return le16(p) | le16(p + 2) << 16;
}

/* a zip entry's name safe below the folder: no drive, no root, no ".." */
static bool safe_name(const char *s)
{
    if (!*s || *s == '/' || *s == '\\' || strchr(s, ':')) {
        return false;
    }
    for (const char *p = s; *p;) {
        const char *e = p + strcspn(p, "/\\");
        if (e - p == 2 && p[0] == '.' && p[1] == '.') {
            return false;
        }
        p = *e ? e + 1 : e;
    }
    return true;
}

static void make_parents(char *path)
{
    for (char *p = path + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            const char c = *p;
            *p = 0;
            SDL_CreateDirectory(path);
            *p = c;
        }
    }
}

/* Every file of the zip (stored or deflated) unpacked under dir; false on
 * anything it can't read whole. */
static bool unzip(const char *zip, const char *dir, char *err, size_t en)
{
    size_t n = 0;
    uint8_t *z = SDL_LoadFile(zip, &n);
    if (!z || n < 22 || n > MAX_ZIP) {
        snprintf(err, en, "not a zip file");
        SDL_free(z);
        return false;
    }
    size_t end = n - 22, stop = n > 22 + 0x10000 ? n - 22 - 0x10000 : 0;
    while (end > stop && le32(z + end) != 0x06054B50u) {
        end--;
    }
    bool ok = le32(z + end) == 0x06054B50u;
    if (!ok) {
        snprintf(err, en, "not a zip file");
    }
    const uint32_t entries = ok ? le16(z + end + 10) : 0;
    size_t at = ok ? le32(z + end + 16) : 0;
    int files = 0;
    for (uint32_t i = 0; ok && i < entries; i++) {
        if (at + 46 > n || le32(z + at) != 0x02014B50u) {
            snprintf(err, en, "damaged zip");
            ok = false;
            break;
        }
        const uint32_t method = le16(z + at + 10), csize = le32(z + at + 20), usize = le32(z + at + 24);
        const uint32_t name_len = le16(z + at + 28), local = le32(z + at + 42);
        const uint32_t skip = 46 + name_len + le16(z + at + 30) + le16(z + at + 32);
        if (at + skip > n || !name_len || name_len > 600) {
            snprintf(err, en, "damaged zip");
            ok = false;
            break;
        }
        char name[640];
        memcpy(name, z + at + 46, name_len);
        name[name_len] = 0;
        at += skip;
        if (name[name_len - 1] == '/' || name[name_len - 1] == '\\') {
            continue;
        }
        if (!safe_name(name)) {
            nc_log("xbased-content", "zip entry %s skipped", name);
            continue;
        }
        if ((size_t)local + 30 > n || le32(z + local) != 0x04034B50u) {
            snprintf(err, en, "damaged zip");
            ok = false;
            break;
        }
        const size_t data = (size_t)local + 30 + le16(z + local + 26) + le16(z + local + 28);
        if (data + csize > n || usize > MAX_ZIP) {
            snprintf(err, en, "damaged zip");
            ok = false;
            break;
        }
        char *out = SDL_malloc(usize ? usize : 1);
        if (method == 0 && csize == usize) {
            memcpy(out, z + data, usize);
        } else if (method == 8) {
            if (stbi_zlib_decode_noheader_buffer(out, (int)usize, (const char *)z + data, (int)csize) != (int)usize) {
                snprintf(err, en, "%s couldn't be unpacked", name);
                ok = false;
            }
        } else {
            snprintf(err, en, "%s: compression method %u isn't supported", name, method);
            ok = false;
        }
        if (ok) {
            char path[1400];
            snprintf(path, sizeof(path), "%s/%s", dir, name);
            for (char *p = path; *p; p++) {
                *p = *p == '\\' ? '/' : *p;
            }
            make_parents(path);
            SDL_IOStream *f = SDL_IOFromFile(path, "wb");
            if (!f || SDL_WriteIO(f, out, usize) != usize) {
                snprintf(err, en, "%s couldn't be written", path);
                ok = false;
            }
            if (f) {
                SDL_CloseIO(f);
            }
            files++;
        }
        SDL_free(out);
    }
    SDL_free(z);
    if (ok && !files) {
        snprintf(err, en, "the zip is empty");
    }
    return ok && files > 0;
}

/* ------------------------------------------------------------ what a zip holds */

static bool has_child(const char *dir, const char *name)
{
    int n = 0;
    bool yes = false;
    char **names = SDL_GlobDirectory(dir, NULL, 0, &n);
    for (int i = 0; names && i < n; i++) {
        yes |= !SDL_strcasecmp(names[i], name);
    }
    SDL_free(names);
    return yes;
}

enum { K_NONE, K_TITLE, K_OFFER, K_UPDATE };

/* The folder to copy and what it is: a title's folder (K_TITLE: its $c, $u
 * and the rest go into TDATA\<title ID>), an offer's (K_OFFER: into $c), an
 * update's files (K_UPDATE: into $u). */
static int find_root(const char *dir, const char *tid, int depth, char *out, size_t n)
{
    if (has_child(dir, "$c") || has_child(dir, "$u")) {
        snprintf(out, n, "%s", dir);
        return K_TITLE;
    }
    int c = 0, kind = K_NONE;
    char **names = SDL_GlobDirectory(dir, NULL, 0, &c);
    for (int i = 0; names && i < c && !kind; i++) {
        char p[1400];
        snprintf(p, sizeof(p), "%s/%s", dir, names[i]);
        if (is_dir(p) && !SDL_strcasecmp(names[i], tid)) {
            snprintf(out, n, "%s", p);
            kind = K_TITLE;
        }
    }
    for (int i = 0; names && i < c && !kind && depth < 5; i++) {
        char p[1400];
        snprintf(p, sizeof(p), "%s/%s", dir, names[i]);
        if (is_dir(p)) {
            kind = find_root(p, tid, depth + 1, out, n);
        }
    }
    SDL_free(names);
    if (!kind && has_child(dir, "ContentMeta.xbx")) {
        snprintf(out, n, "%s", dir);
        kind = K_OFFER;
    } else if (!kind && has_child(dir, "default.xbe")) {
        snprintf(out, n, "%s", dir);
        kind = K_UPDATE;
    }
    return kind;
}

/* A zip installed for the title; false with err set */
static bool install_zip(const char *zip, uint32_t title, char *err, size_t en)
{
    char tmp[1100];
    snprintf(tmp, sizeof(tmp), "%scontent.tmp", cfg_data_dir());
    remove_tree(tmp);
    SDL_CreateDirectory(tmp);
    bool ok = unzip(zip, tmp, err, en);
    if (ok) {
        char tid[16], root[1400], dst[1200], to[1400];
        snprintf(tid, sizeof(tid), "%08x", title);
        const int kind = find_root(tmp, tid, 0, root, sizeof(root));
        title_dir(title, dst, sizeof(dst));
        char hdd[1024], tdata[1100];
        hdd_dir(hdd, sizeof(hdd));
        SDL_CreateDirectory(hdd);
        snprintf(tdata, sizeof(tdata), "%s/Partition1", hdd);
        SDL_CreateDirectory(tdata);
        snprintf(tdata, sizeof(tdata), "%s/Partition1/TDATA", hdd);
        SDL_CreateDirectory(tdata);
        int files = 0;
        switch (kind) {
        case K_TITLE: files = copy_tree(root, dst); break;
        case K_OFFER: {
            const char *leaf = strrchr(root, '/');
            snprintf(to, sizeof(to), "%s/$c/%s", dst, leaf && leaf[1] && strcmp(root, tmp) ? leaf + 1 : "offer");
            SDL_CreateDirectory(dst);
            snprintf(tdata, sizeof(tdata), "%s/$c", dst);
            SDL_CreateDirectory(tdata);
            files = copy_tree(root, to);
            break;
        }
        case K_UPDATE:
            SDL_CreateDirectory(dst);
            snprintf(to, sizeof(to), "%s/$u", dst);
            files = copy_tree(root, to);
            break;
        default: snprintf(err, en, "no title update or downloadable content in it"); ok = false;
        }
        if (ok && !files) {
            snprintf(err, en, "nothing could be copied");
            ok = false;
        }
        if (ok) {
            nc_log("xbased-content", "%s: %d file(s) installed into %s", zip, files, dst);
        }
    }
    remove_tree(tmp);
    return ok;
}

/* ------------------------------------------------------------ the worker */

typedef struct {
    char name[200];     /* as shown */
    char url[700];
} Offer;

static struct {
    SDL_Thread *th;
    SDL_AtomicInt busy, done;
    int job;                        /* 1 install a file, 2 list online, 3 download one */
    uint32_t title;
    char title_name[160];
    char path[1100];                /* job 1: the zip */
    char url[700];                  /* job 3 */
    bool ok;
    char err[600];
    Offer offers[MAX_ITEMS];
    int noffers;
    bool listed;                    /* the online list came */
    uint32_t listed_for;
} W;

static bool curl_to(const char *url, const char *to, const char *secs)
{
    char part[1200];
    snprintf(part, sizeof(part), "%s.part", to);
    const char *args[] = {"curl", "-sfL", "--max-time", secs, "-A", "xbased", "-o", part, url, NULL};
    if (!cfg_run(args)) {
        SDL_RemovePath(part);
        return false;
    }
    SDL_RemovePath(to);
    return SDL_RenamePath(part, to);
}

static void url_decode(const char *s, char *out, size_t n)
{
    size_t o = 0;
    for (; *s && o + 1 < n; s++) {
        if (*s == '%' && isxdigit((unsigned char)s[1]) && isxdigit((unsigned char)s[2])) {
            char h[3] = {s[1], s[2], 0};
            out[o++] = (char)strtol(h, NULL, 16);
            s += 2;
        } else if (!strncmp(s, "&amp;", 5)) {
            out[o++] = '&';
            s += 4;
        } else {
            out[o++] = *s == '+' ? ' ' : *s;
        }
    }
    out[o] = 0;
}

static void lower(char *s)
{
    for (; *s; s++) {
        *s = (char)tolower((unsigned char)*s);
    }
}

/* the page's DLC zips for the title: named with its ID, or (none so) starting with its name */
static void parse_page(const char *html)
{
    char tid[16], tname[160];
    snprintf(tid, sizeof(tid), "%08x", W.title);
    snprintf(tname, sizeof(tname), "%s", W.title_name);
    lower(tname);
    char *cut = strpbrk(tname, "(:[");     /* "Halo 2 (USA)" as "halo 2" */
    if (cut) {
        *cut = 0;
    }
    for (size_t k = strlen(tname); k && tname[k - 1] == ' '; k--) {
        tname[k - 1] = 0;
    }
    Offer by_name[MAX_ITEMS];
    int nn = 0;
    W.noffers = 0;
    for (const char *p = html; (p = strstr(p, "dlc/")) != NULL; p += 4) {
        const char *e = p + 4;
        while (*e && *e != '"' && *e != '\'' && *e != '<' && *e != '>' && *e != ' ' && *e != ')') {
            e++;
        }
        if (e - p < 9 || e - p > 400 || SDL_strncasecmp(e - 4, ".zip", 4)) {
            continue;
        }
        /* the link as written: absolute, or from the page's folder */
        const char *s = p;
        while (s > html && s[-1] != '"' && s[-1] != '\'' && s[-1] != '(' && s[-1] != ' ' && p - s < 200) {
            s--;
        }
        char link[600], url[700], file[300], low[300];
        snprintf(link, sizeof(link), "%.*s", (int)(e - s), s);
        if (!strncmp(link, "http://", 7) || !strncmp(link, "https://", 8)) {
            snprintf(url, sizeof(url), "%s", link);
        } else if (link[0] == '/') {
            snprintf(url, sizeof(url), "https://www.xbmc4gamers.co.uk%s", link);
        } else {
            snprintf(url, sizeof(url), DL_PAGE "%s", link);
        }
        {
            /* a space as written: escaped */
            char esc[700];
            size_t o = 0;
            for (const char *c = url; *c && o + 4 < sizeof(esc); c++) {
                if (*c == ' ') {
                    memcpy(esc + o, "%20", 3);
                    o += 3;
                } else {
                    esc[o++] = *c;
                }
            }
            esc[o] = 0;
            snprintf(url, sizeof(url), "%s", esc);
        }
        char raw[300];
        snprintf(raw, sizeof(raw), "%.*s", (int)(e - p - 8), p + 4);    /* between "dlc/" and ".zip" */
        url_decode(raw, file, sizeof(file));
        snprintf(low, sizeof(low), "%s", file);
        lower(low);
        bool dup = false;
        for (int i = 0; i < W.noffers; i++) {
            dup |= !strcmp(W.offers[i].url, url);
        }
        for (int i = 0; i < nn; i++) {
            dup |= !strcmp(by_name[i].url, url);
        }
        if (dup) {
            continue;
        }
        Offer o;
        snprintf(o.name, sizeof(o.name), "%s", file);
        snprintf(o.url, sizeof(o.url), "%s", url);
        if (strstr(low, tid) && W.noffers < MAX_ITEMS) {
            W.offers[W.noffers++] = o;
        } else if (tname[0] && !strncmp(low, tname, strlen(tname)) && nn < MAX_ITEMS) {
            by_name[nn++] = o;
        }
    }
    for (int i = 0; i < nn && W.noffers < MAX_ITEMS; i++) {
        W.offers[W.noffers++] = by_name[i];
    }
}

static int worker(void *arg)
{
    (void)arg;
    W.ok = false;
    W.err[0] = 0;
    if (W.job == 1) {
        W.ok = install_zip(W.path, W.title, W.err, sizeof(W.err));
    } else if (W.job == 2) {
        char page[1100];
        snprintf(page, sizeof(page), "%sdlc-index.html", cfg_data_dir());
        if (!curl_to(DL_PAGE, page, "30")) {
            snprintf(W.err, sizeof(W.err), "the download site couldn't be reached");
        } else {
            size_t n = 0;
            char *html = SDL_LoadFile(page, &n);
            if (html) {
                parse_page(html);
                SDL_free(html);
                W.ok = true;
                W.listed = true;
                W.listed_for = W.title;
            }
            SDL_RemovePath(page);
        }
    } else if (W.job == 3) {
        char zip[1100];
        snprintf(zip, sizeof(zip), "%sdlc-download.zip", cfg_data_dir());
        if (!curl_to(W.url, zip, "900")) {
            snprintf(W.err, sizeof(W.err), "the download failed");
        } else {
            W.ok = install_zip(zip, W.title, W.err, sizeof(W.err));
        }
        SDL_RemovePath(zip);
    }
    SDL_SetAtomicInt(&W.done, 1);
    SDL_SetAtomicInt(&W.busy, 0);
    return 0;
}

static bool start_job(int job)
{
    if (SDL_GetAtomicInt(&W.busy)) {
        return false;
    }
    if (W.th) {
        SDL_WaitThread(W.th, NULL);
        W.th = NULL;
    }
    W.job = job;
    W.title = D.cur->g.title_id;
    snprintf(W.title_name, sizeof(W.title_name), "%s", D.cur->orig[0] ? D.cur->orig : D.cur->g.title);
    SDL_SetAtomicInt(&W.done, 0);
    SDL_SetAtomicInt(&W.busy, 1);
    W.th = SDL_CreateThread(worker, "xbased-content", NULL);
    if (!W.th) {
        SDL_SetAtomicInt(&W.busy, 0);
        return false;
    }
    return true;
}

static void SDLCALL zip_cb(void *ud, const char *const *files, int filter)
{
    (void)ud;
    (void)filter;
    if (files && files[0] && !SDL_GetAtomicInt(&W.busy)) {
        snprintf(W.path, sizeof(W.path), "%s", files[0]);
        start_job(1);
    }
}

/* ------------------------------------------------------------ what's installed */

typedef struct {
    char name[200], dir[1300];
    bool update;
} Installed;

static Installed g_inst[MAX_ITEMS];
static int g_ninst, g_del;
static uint32_t g_inst_for;
static bool g_inst_dirty = true;

static void load_installed(void)
{
    g_ninst = 0;
    char td[1200];
    title_dir(D.cur->g.title_id, td, sizeof(td));
    int n = 0;
    char **names = SDL_GlobDirectory(td, NULL, 0, &n);
    for (int i = 0; names && i < n; i++) {
        char p[1400];
        snprintf(p, sizeof(p), "%s/%s", td, names[i]);
        if (!SDL_strcasecmp(names[i], "$u") && is_dir(p) && g_ninst < MAX_ITEMS) {
            Installed *e = &g_inst[g_ninst++];
            snprintf(e->name, sizeof(e->name), "%s", T(S_TITLE_UPDATE));
            snprintf(e->dir, sizeof(e->dir), "%s", p);
            e->update = true;
        } else if (!SDL_strcasecmp(names[i], "$c") && is_dir(p)) {
            int m = 0;
            char **offers = SDL_GlobDirectory(p, NULL, 0, &m);
            for (int k = 0; offers && k < m && g_ninst < MAX_ITEMS; k++) {
                char o[1400];
                snprintf(o, sizeof(o), "%s/%s", p, offers[k]);
                if (is_dir(o)) {
                    Installed *e = &g_inst[g_ninst++];
                    snprintf(e->name, sizeof(e->name), "%s %s", T(S_DLC_OFFER), offers[k]);
                    snprintf(e->dir, sizeof(e->dir), "%s", o);
                    e->update = false;
                }
            }
            SDL_free(offers);
        }
    }
    SDL_free(names);
    g_inst_for = D.cur->g.title_id;
    g_inst_dirty = false;
}

static void del_installed(Dash *d)
{
    (void)d;
    if (g_del >= 0 && g_del < g_ninst) {
        remove_tree(g_inst[g_del].dir);
        g_inst_dirty = true;
    }
}

/* ------------------------------------------------------------ the screen */

static bool g_online;       /* the online list shown below */

void scr_content(void)
{
    const float W_ = ui_w(), H = ui_h();
    draw_header(T(S_CONTENT), D.cur->g.title);
    if (g_inst_dirty || g_inst_for != D.cur->g.title_id) {
        if (g_inst_for != D.cur->g.title_id) {
            g_online = false;
        }
        load_installed();
    }
    const bool busy = SDL_GetAtomicInt(&W.busy);
    if (SDL_GetAtomicInt(&W.done)) {
        SDL_SetAtomicInt(&W.done, 0);
        if (W.job == 2) {
            if (!W.ok) {
                dash_toast(W.err);
            } else if (!W.noffers) {
                dash_toast(T(S_DLC_NONE_ONLINE));
            }
        } else if (W.ok) {
            dash_toast(T(S_DLC_INSTALLED));
            g_inst_dirty = true;
        } else {
            char msg[800];
            snprintf(msg, sizeof(msg), "%s: %s", T(S_DLC_FAILED), W.err);
            dash_toast(msg);
        }
    }
    List l;
    list_begin(&l, 74, 150, W_ - 148, H - 244, 58);
    if (list_row(&l, T(S_DLC_FROM_FILE), NULL, NULL) && !busy) {
        dash_browse_file(T(S_DLC_FROM_FILE), "zip", zip_cb, NULL);
    }
    if (list_row(&l, T(S_DLC_FIND_ONLINE), "xbmc4gamers.co.uk", NULL) && !busy) {
        g_online = true;
        start_job(2);
    }
    for (int i = 0; i < g_ninst; i++) {
        list_row(&l, g_inst[i].name, T(S_INSTALLED), NULL);
        if (list_selected(&l) && D.in.x && !busy) {
            g_del = i;
            dash_ask(T(S_CONFIRM_DELETE), del_installed);
        }
    }
    if (g_online && W.listed && W.listed_for == D.cur->g.title_id) {
        for (int i = 0; i < W.noffers; i++) {
            if (list_row(&l, W.offers[i].name, T(S_DOWNLOAD), NULL) && !busy) {
                snprintf(W.url, sizeof(W.url), "%s", W.offers[i].url);
                start_job(3);
            }
        }
    }
    list_end(&l);
    const char *status = busy ? T(W.job == 2 ? S_DLC_SEARCHING : W.job == 3 ? S_DLC_DOWNLOADING : S_DLC_INSTALLING)
                              : T(S_DLC_NOTE);
    ui_text(74, H - 82, status, 18, UI_FONT_REGULAR, UI_GREY, UI_ALIGN_LEFT);
    if (D.in.back) {
        dash_pop();
    }
    draw_hints(T(S_SELECT), T(S_BACK), g_ninst ? T(S_DELETE) : NULL, NULL);
}
