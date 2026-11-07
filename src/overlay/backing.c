// SPDX-License-Identifier: MPL-2.0
// Backing store for everything the overlay draws.
//
// We draw into the game's own screen surface, but the game only repaints cells that changed. So the
// pixels under everything we draw are saved first and put back right after the frame is presented
// (backing_restore, called by the hook after SDL_Flip / SDL_UpdateRect): our drawing reaches the
// screen every frame but never stays in the game's surface, so nothing is left behind when a label,
// tooltip or panel goes away. Same trick as the game's own software mouse cursor (FUN_0041ab60).
#include <stdlib.h>
#include <string.h>
#include "hook/sdl.h"
#include "overlay/backing.h"

typedef struct { int x, y, w, h; size_t off; } saved_rect;
static saved_rect g_saved[512];
static int g_nsaved;
static uint8_t *g_save;
static size_t g_save_cap, g_save_used;

void backing_save(SDL_Surface *s, int x, int y, int w, int h) {
    int bpp = s->format->BytesPerPixel;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > s->w) w = s->w - x;
    if (y + h > s->h) h = s->h - y;
    if (w <= 0 || h <= 0 || !s->pixels || g_nsaved >= (int)(sizeof g_saved / sizeof *g_saved)) return;
    size_t need = (size_t)w * h * bpp;
    if (g_save_used + need > g_save_cap) {
        size_t cap = (g_save_used + need) * 2;
        uint8_t *nb = realloc(g_save, cap);
        if (!nb) return;
        g_save = nb; g_save_cap = cap;
    }
    saved_rect *r = &g_saved[g_nsaved++];
    r->x = x; r->y = y; r->w = w; r->h = h; r->off = g_save_used;
    for (int row = 0; row < h; ++row)
        memcpy(g_save + g_save_used + (size_t)row * w * bpp, (uint8_t *)s->pixels + (size_t)(y + row) * s->pitch + (size_t)x * bpp, (size_t)w * bpp);
    g_save_used += need;
}

void backing_restore(SDL_Surface *s) {
    if (!s || !s->pixels || !s->format) { g_nsaved = 0; g_save_used = 0; return; }
    int bpp = s->format->BytesPerPixel;
    for (int i = g_nsaved - 1; i >= 0; --i) {                 // reverse: overlapping saves unwind correctly
        const saved_rect *r = &g_saved[i];
        for (int row = 0; row < r->h; ++row)
            memcpy((uint8_t *)s->pixels + (size_t)(r->y + row) * s->pitch + (size_t)r->x * bpp, g_save + r->off + (size_t)row * r->w * bpp, (size_t)r->w * bpp);
    }
    g_nsaved = 0;
    g_save_used = 0;
}
