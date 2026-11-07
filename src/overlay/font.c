// SPDX-License-Identifier: MPL-2.0
// Text drawing for the overlay: the game's own fonts (glyph sheets read in place, see cogmind.h
// "UI") and a built-in 3x5 pixel font as the fallback.
#include <stdint.h>
#include <string.h>
#include "cogmind.h"
#include "cm_ui.h"
#include "base/mem.h"
#include "hook/sdl.h"
#include "overlay/font.h"

static uint16_t FONT[128];

// 3x5 glyphs, rows top->bottom, '#' = pixel
static const struct { char c; const char *rows[5]; } FONT_SRC[] = {
    {'A',{".#.","#.#","###","#.#","#.#"}}, {'B',{"##.","#.#","##.","#.#","##."}}, {'C',{".##","#..","#..","#..",".##"}},
    {'D',{"##.","#.#","#.#","#.#","##."}}, {'E',{"###","#..","##.","#..","###"}}, {'F',{"###","#..","##.","#..","#.."}},
    {'G',{".##","#..","#.#","#.#",".##"}}, {'H',{"#.#","#.#","###","#.#","#.#"}}, {'I',{"###",".#.",".#.",".#.","###"}},
    {'J',{"..#","..#","..#","#.#",".#."}}, {'K',{"#.#","#.#","##.","#.#","#.#"}}, {'L',{"#..","#..","#..","#..","###"}},
    {'M',{"#.#","###","###","#.#","#.#"}}, {'N',{"##.","#.#","#.#","#.#","#.#"}}, {'O',{".#.","#.#","#.#","#.#",".#."}},
    {'P',{"##.","#.#","##.","#..","#.."}}, {'Q',{".#.","#.#","#.#","##.",".##"}}, {'R',{"##.","#.#","##.","#.#","#.#"}},
    {'S',{".##","#..",".#.","..#","##."}}, {'T',{"###",".#.",".#.",".#.",".#."}}, {'U',{"#.#","#.#","#.#","#.#","###"}},
    {'V',{"#.#","#.#","#.#","#.#",".#."}}, {'W',{"#.#","#.#","###","###","#.#"}}, {'X',{"#.#","#.#",".#.","#.#","#.#"}},
    {'Y',{"#.#","#.#",".#.",".#.",".#."}}, {'Z',{"###","..#",".#.","#..","###"}},
    {'0',{"###","#.#","#.#","#.#","###"}}, {'1',{".#.","##.",".#.",".#.","###"}}, {'2',{"##.","..#",".#.","#..","###"}},
    {'3',{"##.","..#",".#.","..#","##."}}, {'4',{"#.#","#.#","###","..#","..#"}}, {'5',{"###","#..","##.","..#","##."}},
    {'6',{".##","#..","###","#.#","###"}}, {'7',{"###","..#",".#.",".#.",".#."}}, {'8',{"###","#.#","###","#.#","###"}},
    {'9',{"###","#.#","###","..#","##."}}, {':',{"...",".#.","...",".#.","..."}}, {'/',{"..#","..#",".#.","#..","#.."}},
    {',',{"...","...","...",".#.","#.."}}, {'.',{"...","...","...","...",".#."}}, {'-',{"...","...","###","...","..."}},
    {'%',{"#.#","..#",".#.","#..","#.#"}}, {'(',{".#.","#..","#..","#..",".#."}}, {')',{".#.","..#","..#","..#",".#."}},
    {'=',{"...","###","...","###","..."}}, {'@',{".#.","#.#","###","#..",".##"}},
    {'+',{"...",".#.","###",".#.","..."}}, {'!',{".#.",".#.",".#.","...",".#."}}, {'?',{"##.","..#",".#.","...",".#."}},
    {'>',{"#..",".#.","..#",".#.","#.."}}, {'<',{"..#",".#.","#..",".#.","..#"}}, {'\'',{".#.",".#.","...","...","..."}},
    {'[',{"##.","#..","#..","#..","##."}}, {']',{".##","..#","..#","..#",".##"}}, {'*',{"...","#.#",".#.","#.#","..."}},
    {'_',{"...","...","...","...","###"}}, {'#',{"#.#","###","#.#","###","#.#"}}, {'~',{"...","..#","###","#..","..."}},
};

void font_init(void) {
    for (size_t i = 0; i < sizeof FONT_SRC / sizeof *FONT_SRC; ++i) {
        uint16_t bits = 0;
        for (int r = 0; r < 5; ++r)
            for (int c = 0; c < 3; ++c)
                if (FONT_SRC[i].rows[r][c] == '#') bits |= 1 << (14 - (r * 3 + c));
        FONT[(int)FONT_SRC[i].c] = bits;
    }
}

// ---- built-in 3x5 font ----
void font_px_draw(SDL_Surface *s, int x, int y, const char *str, uint32_t col) {
    for (; *str; ++str, x += 4 * FONT_PX_SCALE) {
        if ((*str & 0xc0) == 0x80) { x -= 4 * FONT_PX_SCALE; continue; }   // UTF-8: one blank cell per character
        int ch = *str >= 'a' && *str <= 'z' ? *str - 32 : *str;
        uint16_t bits = (ch > 0 && ch < 128) ? FONT[ch] : 0;
        for (int row = 0; row < 5; ++row)
            for (int c = 0; c < 3; ++c)
                if (bits & (1 << (14 - (row * 3 + c)))) {
                    SDL_Rect r = { x + c * FONT_PX_SCALE, y + row * FONT_PX_SCALE, FONT_PX_SCALE, FONT_PX_SCALE };
                    real_FillRect(s, &r, col);
                }
    }
}

// ---- the game's own fonts ----

// Game font `idx` (white glyphs, shape in alpha). Re-validated on every use (a handful of guarded
// reads): the game rebuilds fonts on mode changes.
int font_load(int idx, game_font *out) {
    if (idx < 0) return 0;
    uint32_t mgr = rd32(ADDR_FONT_MGR), v[2], fd[3];
    if (!mgr || !rd(mgr + FONTMGR_FONTS, v, 8) || v[1] <= v[0] || (v[1] - v[0]) / 4 <= (uint32_t)idx) return 0;
    uint32_t f = rd32(v[0] + 4 * idx);
    if (!f || !rd(f + XFONT_SURFACE, fd, sizeof fd)) return 0;
    SDL_Surface sf;
    SDL_PixelFormat pf;
    int gw = (int)fd[1], gh = (int)fd[2];
    if (!fd[0] || !rd(fd[0], &sf, sizeof sf) || !sf.format || !rd((uint32_t)(uintptr_t)sf.format, &pf, sizeof pf)) return 0;
    if (pf.BytesPerPixel != 4 || !pf.Amask || gw < 4 || gw > 128 || gh < 4 || gh > 128) return 0;
    if (sf.w % gw || sf.w / gw < 8 || sf.h < 4 * gh || sf.pitch < sf.w * 4 || !sf.pixels) return 0;
    uint8_t probe;
    if (!rd((uint32_t)(uintptr_t)sf.pixels + (uint32_t)sf.pitch * (uint32_t)(sf.h / gh * gh) - 1, &probe, 1)) return 0;
    out->pixels = sf.pixels; out->pitch = sf.pitch; out->gw = gw; out->gh = gh; out->ashift = pf.Ashift;
    out->cols = sf.w / gw; out->rows = sf.h / gh;
    return 1;
}

static int glyph_index(unsigned char c) {
    if (c >= 32 && c <= 63) return c - 32;
    if (c >= 'A' && c <= 'Z') return 64 + c - 'A';
    if (c >= 'a' && c <= 'z') return 96 + c - 'a';           // the game's small caps
    switch (c) {
    case '@': return 32; case '[': return 33; case '\\': return 34; case ']': return 35; case '^': return 36;
    case '_': return 37; case '`': return 38; case '{': return 39; case '|': return 40; case '}': return 41;
    case '~': return 42;
    }
    return 0;                                                 // space
}

// alpha-blend glyph gi of font f onto a 32-bit screen surface, tinted rgb
void font_blit_glyph(SDL_Surface *s, int x, int y, const game_font *gf, int gi, uint32_t rgb) {
    const SDL_PixelFormat *f = s->format;
    const int r = rgb >> 16 & 0xff, g = rgb >> 8 & 0xff, b = rgb & 0xff;
    const uint32_t keep = ~(f->Rmask | f->Gmask | f->Bmask);
    if (gi < 0 || gi >= gf->cols * gf->rows || x < 0 || x + gf->gw > s->w) return;
    const uint8_t *src = gf->pixels + (gi / gf->cols) * gf->gh * gf->pitch + (gi % gf->cols) * gf->gw * 4;
    for (int yy = 0; yy < gf->gh; ++yy) {
        int dy = y + yy;
        if (dy < 0 || dy >= s->h) continue;
        const uint32_t *sp = (const uint32_t *)(src + yy * gf->pitch);
        uint32_t *dp = (uint32_t *)((uint8_t *)s->pixels + dy * s->pitch) + x;
        for (int xx = 0; xx < gf->gw; ++xx) {
            int a = sp[xx] >> gf->ashift & 0xff;
            if (!a) continue;
            uint32_t d = dp[xx];
            int dr = (d & f->Rmask) >> f->Rshift, dg = (d & f->Gmask) >> f->Gshift, db = (d & f->Bmask) >> f->Bshift;
            dr += (r - dr) * a / 255; dg += (g - dg) * a / 255; db += (b - db) * a / 255;
            dp[xx] = (d & keep) | ((uint32_t)dr << f->Rshift) | ((uint32_t)dg << f->Gshift) | ((uint32_t)db << f->Bshift);
        }
    }
}

// text in the overlay font (ASCII mapped to the UI font's layout)
// next character of UTF-8 text as a UI font glyph (0 = blank), advancing *s. Box drawing maps to the
// game's own frame glyphs (codes 0x80-0x97, whose glyph index in the UI font is the code itself);
// anything else outside ASCII draws as '?'.
static int next_glyph(const char **s) {
    const unsigned char *p = (const unsigned char *)*s;
    uint32_t u = *p++;
    if (u >= 0xc0) {
        int n = u >= 0xf0 ? 3 : u >= 0xe0 ? 2 : 1;
        u &= 0x3f >> n;
        while (n-- && (*p & 0xc0) == 0x80) u = u << 6 | (*p++ & 0x3f);
    }
    *s = (const char *)p;
    if (u < 0x80) return glyph_index((unsigned char)u);
    for (int k = 0; k < 0x18; ++k) if (CM_CP437_HI[k] == u) return 0x80 + k;
    return glyph_index('?');
}

int font_text_cells(const char *str) {
    int n = 0;
    for (; *str; ++str) n += (*str & 0xc0) != 0x80;
    return n;
}

void font_draw(SDL_Surface *s, int x, int y, const game_font *gf, const char *str, uint32_t rgb) {
    for (; *str; x += gf->gw) {
        int gi = next_glyph(&str);
        if (gi) font_blit_glyph(s, x, y, gf, gi, rgb);
    }
}
