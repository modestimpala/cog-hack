// SPDX-License-Identifier: MPL-2.0
// Stats overlay + plugin panels, drawn onto the game's screen surface just before it is presented
// (SDL_Flip / SDL_UpdateRect). Text is rebuilt at most every REFRESH_MS; drawing happens every frame.
//
// Text uses the game's own UI font (its glyph sheet, read in place: see cogmind.h "UI") so panels
// look native; if that can't be validated it falls back to a built-in 3x5 pixel font.
// Config: overlay_font = game font index (default 0, -1 = built-in), overlay_pos = 0 top-left, 1 bottom-left,
// overlay_stats = 1 for the stats/hotkeys box (off by default; plugin panels and labels draw regardless).
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "cogmind.h"
#include "cm_ui.h"
#include "base/config.h"
#include "base/log.h"
#include "base/mem.h"
#include "game/player.h"
#include "hook/sdl.h"
#include "overlay/backing.h"
#include "overlay/font.h"
#include "overlay/overlay.h"
#include "overlay/panels.h"

#define REFRESH_MS 200

static int g_visible;
void overlay_init(int visible) {
    g_visible = visible;
    font_init();
}

static game_font g_gf;   // the overlay's font, re-read each frame
static int load_game_font(void) { return font_load(cfg_int("overlay_font", 0), &g_gf) && g_gf.cols == 32; }

void overlay_restore(SDL_Surface *s) { backing_restore(s); }

void overlay_toggle(void) { g_visible = !g_visible; hlog("overlay -> %d", g_visible); }
int  overlay_visible(void) { return g_visible; }

// ---------------- layout ----------------
typedef struct { SDL_Surface *s; int game, cw, lh; } pen;

static void pen_text(pen *p, int x, int y, const char *t, uint32_t rgb) {
    if (p->game) font_draw(p->s, x, y, &g_gf, t, rgb);
    else font_px_draw(p->s, x, y + 2, t, real_MapRGB(p->s->format, rgb >> 16 & 0xff, rgb >> 8 & 0xff, rgb & 0xff));
}

static char g_lines[2][112];
static int g_nlines;
static DWORD g_built_at;

static void rebuild_text(void) {
    player_info pi;
    if (read_player(&pi)) {
        char loc[40] = "";
        location_text(loc, sizeof loc);
        snprintf(g_lines[0], sizeof g_lines[0], "Core %d/%d  En %d/%d  Mat %d/%d  Heat %d  Corr %d%%",
                 pi.core, player_max_core(), pi.energy, player_max_energy(), pi.matter, player_max_matter(),
                 player_heat(), pi.corruption);
        snprintf(g_lines[1], sizeof g_lines[1], "%s  Time %d  @(%d,%d)  ^F9 console  ^F10 dump  ^F11 snap",
                 loc, game_time(), pi.x, pi.y);
        g_nlines = 2;
    } else {
        snprintf(g_lines[0], sizeof g_lines[0], "cog-hack  (no run)");
        g_nlines = 1;
    }
}

// one box of '\n'-separated lines; returns its height. y_up: grow upward from y (bottom anchor).
static int box(pen *p, int x, int y, int y_up, const char *const *lines, int n, uint32_t bg, uint32_t fg, uint32_t warn) {
    size_t w = 0;
    for (int i = 0; i < n; ++i) if ((size_t)font_text_cells(lines[i]) > w) w = (size_t)font_text_cells(lines[i]);
    int h = n * p->lh + 8;
    if (y_up) y -= h;
    SDL_Rect r = { (int16_t)x, (int16_t)y, (uint16_t)(w * p->cw + 12), (uint16_t)h };
    backing_save(p->s, r.x, r.y, r.w, r.h);
    real_FillRect(p->s, &r, real_MapRGB(p->s->format, bg >> 16 & 0xff, bg >> 8 & 0xff, bg & 0xff));
    for (int i = 0; i < n; ++i) pen_text(p, x + 6, y + 4 + i * p->lh, lines[i], lines[i][0] == '!' ? warn : fg);
    return h;
}

// plugin/script labels, anchored to root UI cells (same mapping as the game's mouse: FUN_00418b50)
static void draw_labels(pen *p) {
    int gw = 16, gh = 32;
    if (!cm_ui_font_size(rd, 0, &gw, &gh)) return;
    int ox = rdi(ADDR_DISPLAY + DISPLAY_ORIGIN_X), oy = rdi(ADDR_DISPLAY + DISPLAY_ORIGIN_Y);
    for (int i = 0; i < label_count(); ++i) {
        const ui_label *l = label_get(i);
        if (!l) continue;
        char line[1024];
        const char *q = l->text;
        for (int row = 0; (*q || (row == 0 && l->tile_font >= 0)) && row < 32; ++row) {   // a tile-only label: one row
            const char *e = strchr(q, '\n');
            size_t len = e ? (size_t)(e - q) : strlen(q);
            if (len > sizeof line - 1) len = sizeof line - 1;
            memcpy(line, q, len);
            line[len] = 0;
            int x = ox + l->cx * gw, y = oy + (l->cy + row) * gh;
            game_font tf;
            int tile = row == 0 && l->tile_font >= 0 && font_load(l->tile_font, &tf) && tf.gh <= gh * 2;
            int tile_w = tile ? (tf.gw + gw - 1) / gw * gw : 0;        // the tile takes whole root cells
            int w_px = tile_w + font_text_cells(line) * gw;
            if (w_px) backing_save(p->s, x, y, w_px, tile ? (tf.gh > gh ? tf.gh : gh) : gh);
            if (l->bg != LABEL_NO_BG && w_px) {
                SDL_Rect r = { (int16_t)x, (int16_t)y, (uint16_t)w_px, (uint16_t)gh };
                real_FillRect(p->s, &r, real_MapRGB(p->s->format, l->bg >> 16 & 0xff, l->bg >> 8 & 0xff, l->bg & 0xff));
            }
            if (tile) { font_blit_glyph(p->s, x + (tile_w - tf.gw) / 2, y + (gh - tf.gh) / 2, &tf, l->tile_glyph, l->tile_fg); x += tile_w; }
            pen_text(p, x, y + (p->game ? 0 : (gh - 5 * FONT_PX_SCALE) / 2), line, l->fg);
            q = e ? e + 1 : q + len;
        }
    }
}

void overlay_draw(SDL_Surface *s) {
    if (!g_visible || !s || !s->format || !real_FillRect || !real_MapRGB) return;
    DWORD now = GetTickCount();
    if (cfg_int("overlay_stats", 0) && (!g_built_at || now - g_built_at >= REFRESH_MS)) { rebuild_text(); g_built_at = now; }

    pen p = { s, 0, 4 * FONT_PX_SCALE, 6 * FONT_PX_SCALE };
    if (s->format->BytesPerPixel == 4 && s->pixels && load_game_font()) { p.game = 1; p.cw = g_gf.gw; p.lh = g_gf.gh; }
    int bottom = cfg_int("overlay_pos", 1) == 1;

    // boxes: stats first, then each plugin panel (lines split on '\n', max 16 per panel)
    enum { MAXB = 5, MAXL = 16 };
    static char text[MAXB][MAXL][160];
    static const char *ptr[MAXB][MAXL];
    int nb = 0, cnt[MAXB], stats = cfg_int("overlay_stats", 0);
    if (stats) {
        for (int i = 0; i < g_nlines; ++i) ptr[0][i] = g_lines[i];
        cnt[nb++] = g_nlines;
    }
    for (int i = 0; i < panel_count() && nb < MAXB; ++i) {
        const char *q = panel_text(i);
        if (!q) continue;
        int n = 0;
        while (*q && n < MAXL) {
            const char *e = strchr(q, '\n');
            size_t len = e ? (size_t)(e - q) : strlen(q);
            if (len > sizeof text[0][0] - 1) len = sizeof text[0][0] - 1;
            memcpy(text[nb][n], q, len);
            text[nb][n][len] = 0;
            ptr[nb][n] = text[nb][n];
            ++n;
            q = e ? e + 1 : q + strlen(q);
        }
        cnt[nb++] = n;
    }
    draw_labels(&p);
    // stack: top-left grows down; bottom-left grows up (stats box nearest the corner)
    int y = bottom ? s->h - 4 : 4;
    for (int i = 0; i < nb; ++i) {
        int is_stats = stats && i == 0;
        int h = box(&p, 4, y, bottom, ptr[i], cnt[i],
                    is_stats ? 0x200020 : 0x181000, is_stats ? 0xFF70FF : 0xFFC040, 0xFF5040);
        y += bottom ? -(h + 4) : h + 4;
    }
}
