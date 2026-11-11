/*
 * xbased front end — an achievement's picture (cheevo_art.h).
 */
#include "native/app/ui/cheevo_art.h"
#include "native/app/ui/config.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include "stb_image_write.h"

#define SIDE 128
#define CACHE 32

static UiTex *g_x;
static struct {
    char badge[64];
    UiTex *tex;         /* NULL: no file (the X) */
} g_cache[CACHE];
static int g_ncache;

static float clampf(float v)
{
    return v < 0 ? 0 : v > 1 ? 1 : v;
}

/* the distance from (px, py) to the segment (ax, ay)-(bx, by) */
static float seg(float px, float py, float ax, float ay, float bx, float by)
{
    const float vx = bx - ax, vy = by - ay;
    float k = ((px - ax) * vx + (py - ay) * vy) / (vx * vx + vy * vy);
    k = clampf(k);
    const float dx = px - (ax + k * vx), dy = py - (ay + k * vy);
    return sqrtf(dx * dx + dy * dy);
}

/* the placeholder: a big green X on a dark green tile, its edges smoothed */
static UiTex *make_x(void)
{
    static uint8_t px[SIDE * SIDE * 4];
    const float r = SIDE * 0.16f, arm = SIDE * 0.13f, m = SIDE * 0.27f;
    for (int y = 0; y < SIDE; y++) {
        for (int x = 0; x < SIDE; x++) {
            const float fx = x + 0.5f, fy = y + 0.5f;
            /* the tile: a rounded square, a little lighter at the top */
            const float dx = fmaxf(fabsf(fx - SIDE / 2.0f) - (SIDE / 2.0f - r), 0);
            const float dy = fmaxf(fabsf(fy - SIDE / 2.0f) - (SIDE / 2.0f - r), 0);
            const float tile = clampf(r - sqrtf(dx * dx + dy * dy) + 0.5f);
            const float shade = 1 - fy / SIDE * 0.45f;
            float cr = 0x10 * shade, cg = 0x30 * shade, cb = 0x0A * shade;
            /* the X: two strokes from corner to corner, inset, round-ended */
            const float d = fminf(seg(fx, fy, m, m, SIDE - m, SIDE - m), seg(fx, fy, SIDE - m, m, m, SIDE - m));
            const float stroke = clampf(arm / 2 - d + 0.5f);
            const float glow = clampf(1 - d / (arm * 1.9f)) * 0.30f;
            /* bright green, lighter down its middle */
            const float hi = clampf(1 - d / (arm / 2)) * 0.35f;
            const float sr = 0x6C + 0x80 * hi, sg = 0xE0 + 0x1F * hi, sb = 0x24 + 0x60 * hi;
            cr = cr + (0x40 - cr) * glow;
            cg = cg + (0xA0 - cg) * glow;
            cr = cr + (sr - cr) * stroke;
            cg = cg + (sg - cg) * stroke;
            cb = cb + (sb - cb) * stroke;
            uint8_t *o = &px[(y * SIDE + x) * 4];
            o[0] = (uint8_t)fminf(cr, 255);
            o[1] = (uint8_t)fminf(cg, 255);
            o[2] = (uint8_t)fminf(cb, 255);
            o[3] = (uint8_t)(tile * 255);
        }
    }
    return ui_texture_rgba(px, SIDE, SIDE);
}

static UiTex *load_badge(const char *badge)
{
    const char *dirs[2] = {cfg_data_dir(), cfg_exe_dir()};
    for (int i = 0; i < 2; i++) {
        char p[1200];
        snprintf(p, sizeof(p), "%sachievements/badges/%s.png", dirs[i], badge);
        size_t n = 0;
        void *file = SDL_LoadFile(p, &n);
        if (!file) {
            continue;
        }
        int w = 0, h = 0, comp = 0;
        stbi_uc *px = stbi_load_from_memory(file, (int)n, &w, &h, &comp, 4);
        SDL_free(file);
        if (px) {
            UiTex *t = ui_texture_rgba(px, w, h);
            stbi_image_free(px);
            return t;
        }
    }
    return NULL;
}

UiTex *cheevo_art(const char *badge)
{
    if (!g_x) {
        g_x = make_x();
    }
    if (!badge || !badge[0]) {
        return g_x;
    }
    for (int i = 0; i < g_ncache; i++) {
        if (!strcmp(g_cache[i].badge, badge)) {
            return g_cache[i].tex ? g_cache[i].tex : g_x;
        }
    }
    UiTex *t = load_badge(badge);
    if (g_ncache < CACHE) {
        snprintf(g_cache[g_ncache].badge, sizeof(g_cache[0].badge), "%s", badge);
        g_cache[g_ncache++].tex = t;
    }
    return t ? t : g_x;
}

void cheevo_art_free(void)
{
    for (int i = 0; i < g_ncache; i++) {
        if (g_cache[i].tex) {
            ui_texture_free(g_cache[i].tex);
        }
    }
    g_ncache = 0;
    if (g_x) {
        ui_texture_free(g_x);
        g_x = NULL;
    }
}

int cheevo_bmp_to_jpg(const char *bmp, const char *jpg)
{
    size_t n = 0;
    uint8_t *file = SDL_LoadFile(bmp, &n);
    if (!file) {
        return 0;
    }
    /* whole yet? (the window writes it at its next refresh) */
    const uint32_t want = n >= 6 ? (uint32_t)file[2] | (uint32_t)file[3] << 8 | (uint32_t)file[4] << 16 |
                                       (uint32_t)file[5] << 24
                                 : 0;
    if (n < 54 || n < want) {
        SDL_free(file);
        return 0;
    }
    int w = 0, h = 0, comp = 0;
    stbi_uc *px = stbi_load_from_memory(file, (int)n, &w, &h, &comp, 3);
    SDL_free(file);
    const int ok = px && stbi_write_jpg(jpg, w, h, 3, px, 90);
    stbi_image_free(px);
    SDL_RemovePath(bmp);
    return ok ? 1 : -1;
}
