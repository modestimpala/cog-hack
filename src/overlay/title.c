// SPDX-License-Identifier: MPL-2.0
// COG-HACK on the title screen. The game's logo animation lights the letters of COGMIND last (see the
// UI recordings, README "UI recordings"); from that moment the whole logo glitches for a moment
// and is redrawn as our own COG-HACK with its own circuits (art/coghack.txt, built into title_art.inc),
// in the title's font and colours and with the game's own sparkle-in: circuits flash bright and
// settle to their dim green, the letters fade up to 0,217,0 on 0,64,0.
#include <windows.h>
#include <string.h>
#include "cogmind.h"
#include "cm_ui.h"
#include "base/config.h"
#include "base/log.h"
#include "base/mem.h"
#include "hook/sdl.h"
#include "overlay/backing.h"
#include "overlay/font.h"
#include "overlay/title.h"
#include "title_art.inc"

#define CANVAS_W 69
#define CANVAS_H 28
#define GLITCH_MS 350
#define DRAW_MS 550

enum { IDLE, GLITCH, DRAW, STEADY };
static int g_state;
static uint32_t g_title, g_played;
static DWORD g_t;

// per output cell: COVERED = the game draws something here (we black it out), SHOWN = that something
// is visible (it gets the glitch), ON = we draw this cell
enum { COVERED = 1, SHOWN = 2, ON = 4 };
typedef struct { uint8_t flags, glyph; uint32_t fg, bg; } out_cell;
static out_cell g_out[CANVAS_H][TITLE_ART_W];
static uint8_t g_cells[CANVAS_W * CANVAS_H * CELL_SIZE];

// a visible direct child of XRoot by class (the title and the boot sequence are top-level windows)
static uint32_t root_child(const char *cls) {
    cm_ui_node r;
    uint32_t root = cm_ui_root(rd);
    if (!root || !cm_ui_node_read(rd, root, &r)) return 0;
    for (uint32_t i = 0; i < r.nkids; ++i) {
        uint32_t a = cm_ui_child(rd, &r, i);
        cm_ui_node n;
        if (cm_ui_node_read(rd, a, &n) && !n.hidden && !strcmp(n.cls, cls)) return a;
    }
    return 0;
}

static uint32_t hash(uint32_t x, uint32_t y, uint32_t salt) {
    uint32_t h = x * 0x9E3779B1u ^ (y + 0x7F4A7C15u) * 0x85EBCA77u ^ salt * 0xC2B2AE3Du;
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12;
    return h;
}

// the title's visible CTitleAnimated layers in draw order (the last one holds the letters)
static int layers(uint32_t title, cm_ui_node *out, int max) {
    cm_ui_node t;
    int n = 0;
    if (!cm_ui_node_read(rd, title, &t)) return 0;
    for (uint32_t i = 0; i < t.nkids && n < max; ++i) {
        cm_ui_node c;
        if (cm_ui_node_read(rd, cm_ui_child(rd, &t, i), &c) && !c.hidden && !strcmp(c.cls, "CTitleAnimated") &&
            c.w == CANVAS_W && c.h == CANVAS_H) out[n++] = c;
    }
    return n;
}

// reads one layer. lit: every glyph cell is at the letters' final colour. mark: flag the cells we cover.
static int scan(const cm_ui_node *n, int mark, int *lit) {
    if (!rd(n->cells, g_cells, sizeof g_cells)) return 0;
    int any = 0, all = 1;
    for (int x = 0; x < CANVAS_W; ++x)
        for (int y = 0; y < CANVAS_H; ++y) {
            const uint8_t *c = g_cells + (x * CANVAS_H + y) * CELL_SIZE;
            uint32_t ch = *(const uint32_t *)(c + CELL_CHAR);
            const uint8_t *fg = c + CELL_FG, *bg = c + CELL_BG;
            int transparent = CELL_IS_TRANSPARENT(bg);
            int glyph = ch && ch != ' ';
            if (glyph) { any = 1; if (fg[0] || fg[1] != 217 || fg[2]) all = 0; }
            if (mark && x >= TITLE_ART_X0 && !transparent) {
                out_cell *o = &g_out[y][x - TITLE_ART_X0];
                o->flags |= COVERED;
                if ((glyph && (fg[0] | fg[1] | fg[2])) || (bg[0] | bg[1] | bg[2])) o->flags |= SHOWN;
            }
        }
    if (lit) *lit = any && all;
    return 1;
}

int title_draw(SDL_Surface *s) {
    if (!cfg_int("title_art", 1) || !s || !s->format || s->format->BytesPerPixel != 4 || !s->pixels) return 0;
    uint32_t title = root_child("CTitle");
    if (!title) { g_state = IDLE; g_title = 0; return 0; }
    cm_ui_node L[4];
    int n = layers(title, L, 4);
    if (n < 2) return 0;
    DWORD now = GetTickCount();
    if (g_state == IDLE) {
        int lit = 0;
        if (title == g_played || !scan(&L[n - 1], 0, &lit) || !lit) return 0;
        g_state = GLITCH; g_t = now; g_title = g_played = title;
        hlog("title: the logo is lit, hacking it");
        return 0;
    }
    if (title != g_title) { g_state = IDLE; return 0; }
    game_font gf;
    int rgw, rgh;
    if (!font_load(L[0].font, &gf) || !cm_ui_font_size(rd, 0, &rgw, &rgh)) return 0;
    DWORD el = now - g_t;
    if (g_state == GLITCH && el >= GLITCH_MS) { g_state = DRAW; g_t = now; el = 0; }
    if (g_state == DRAW && el >= DRAW_MS) g_state = STEADY;

    memset(g_out, 0, sizeof g_out);
    for (int i = 0; i < n; ++i) scan(&L[i], 1, NULL);
    for (int y = 0; y < CANVAS_H; ++y)
        for (int x = 0; x < TITLE_ART_W; ++x)
            if (g_out[y][x].flags & COVERED) { g_out[y][x].flags |= ON; g_out[y][x].glyph = 0; g_out[y][x].fg = g_out[y][x].bg = 0; }
    if (g_state == GLITCH) {
        // the game's logo cells (and some of its hidden traces) flicker with noise that thins out
        static const uint8_t G[] = { 179, 196, 197, 186, 205, 206, 219, 176, 177, 178, '0', '1', ':', '-', '=' };
        uint32_t frame = now / 33;
        for (int y = 0; y < CANVAS_H; ++y)
            for (int x = 0; x < TITLE_ART_W; ++x) {
                out_cell *o = &g_out[y][x];
                if (!(o->flags & COVERED)) continue;
                uint32_t h = hash((uint32_t)x, (uint32_t)y, frame);
                uint32_t p = (GLITCH_MS - el) * 255 / GLITCH_MS;           // 255 -> 0 over the glitch
                if (!(o->flags & SHOWN)) p /= 4;
                if ((h & 0xff) >= p) continue;
                o->glyph = G[(h >> 8) % sizeof G];
                o->fg = (h >> 24) % 7 == 0 ? 0xb20000u : (uint32_t)(48 + (h >> 16) % 170) << 8;
                o->bg = ((h >> 20) & 7) == 0 ? 0x004000u : 0;
            }
    } else {
        for (int i = 0; i < TITLE_ART_N; ++i) {
            int x = TITLE_ART[i].x - TITLE_ART_X0, y = TITLE_ART[i].y;
            out_cell *o = &g_out[y][x];
            uint32_t h = hash(TITLE_ART[i].x, y, 1);
            if (TITLE_ART[i].kind == 0) {
                // a circuit cell appears at its own moment in the first part of the draw, flashes
                // bright and settles to its level over 250 ms
                DWORD at = (h % 100) * (DRAW_MS * 3 / 5) / 100, age;
                if (g_state != STEADY && el < at) continue;
                age = g_state == STEADY ? 1000 : el - at;
                int target = TITLE_ART[i].bright * 16, v = age >= 250 ? target : target + (178 - target) * (250 - (int)age) / 250;
                o->flags |= ON; o->glyph = TITLE_ART[i].glyph; o->fg = (uint32_t)v << 8; o->bg = 0;
            } else {
                // the letters pop in dark together and fade up, each at a slightly different rate
                DWORD at = DRAW_MS * 2 / 5, age;
                if (g_state != STEADY && el < at) continue;
                age = g_state == STEADY ? 1000 : el - at;
                int v = 30 + (int)(187 * age / (DRAW_MS - at + h % 150));
                if (v > 217) v = 217;
                o->flags |= ON; o->glyph = TITLE_ART[i].glyph; o->fg = (uint32_t)v << 8; o->bg = 0x004000u;
            }
        }
    }
    // draw: one backing save per row span
    int px0 = rdi(ADDR_DISPLAY + DISPLAY_ORIGIN_X) + L[0].ax * rgw + TITLE_ART_X0 * gf.gw;
    int py0 = rdi(ADDR_DISPLAY + DISPLAY_ORIGIN_Y) + L[0].ay * rgh;
    int drew = 0;
    for (int y = 0; y < CANVAS_H; ++y) {
        int first = -1, last = -1;
        for (int x = 0; x < TITLE_ART_W; ++x) if (g_out[y][x].flags & ON) { if (first < 0) first = x; last = x; }
        if (first < 0) continue;
        backing_save(s, px0 + first * gf.gw, py0 + y * gf.gh, (last - first + 1) * gf.gw, gf.gh);
        for (int x = first; x <= last; ++x) {
            const out_cell *o = &g_out[y][x];
            if (!(o->flags & ON)) continue;
            SDL_Rect r = { (int16_t)(px0 + x * gf.gw), (int16_t)(py0 + y * gf.gh), (uint16_t)gf.gw, (uint16_t)gf.gh };
            real_FillRect(s, &r, real_MapRGB(s->format, o->bg >> 16 & 0xff, o->bg >> 8 & 0xff, o->bg & 0xff));
            if (o->glyph) font_blit_glyph(s, r.x, r.y, &gf, o->glyph, o->fg);
        }
        drew = 1;
    }
    return drew;
}
