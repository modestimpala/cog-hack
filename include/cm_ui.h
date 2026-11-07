/* SPDX-License-Identifier: MIT */
/* cm_ui.h: read the game's UI (header-only): the console tree with class names and bounds, the
 * text in any console or screen region, the mouse cell and the element the game considers hovered.
 *
 * Cogmind's UI (the REX engine) is a tree of XConsole objects: XRoot -> CMission -> CHud, CParts,
 * CInventory, CLog, CMap, CInfo... down to single rows (CPart, CHudData, CLogMsg) and buttons.
 * Each one owns a cell grid; XRoot's grid is the composited screen. Positions/sizes are in root
 * cells (16x32 px each at 3440x1440; see cm_ui_mouse for the pixel mapping).
 *
 *   uint32_t root = cm_ui_root(rd);
 *   uint32_t parts = cm_ui_find(rd, root, "CParts", 1);
 *   cm_ui_node n; cm_ui_node_read(rd, parts, &n);  // n.cls, n.ax, n.ay, n.w, n.h, n.hidden
 *   char row[256]; cm_ui_row_text(rd, &n, 0, row, sizeof row);
 *   cm_screen_text(rd, 154, 0, 60, 9, buf, sizeof buf);   // the HUD, as text
 *
 * `rd` is any reader with the host's read_mem signature: api->read_mem in a plugin, the host's
 * rd() in the core, a dump reader in tests/replay.c. Read-only. Call on the game thread.
 * Text is UTF-8 (frame glyphs decoded to box drawing); NUL cells read as spaces, map/icon tiles
 * (cells in a tile font) as U+00B7 unless the tile is plain ASCII.
 */
#ifndef CM_UI_H
#define CM_UI_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "cogmind.h"

typedef int (__cdecl *cm_read_fn)(uint32_t addr, void *out, size_t n);

typedef struct {
    uint32_t addr, parent, vtable, cells, kids, nkids;
    char cls[40];              /* "CParts" ("?" if the RTTI can't be read) */
    int  x, y;                 /* relative to the parent, root cells */
    int  ax, ay;               /* absolute position, root cells */
    int  w, h;                 /* size in the console's OWN cells (its font: CMap uses 64x64 tiles) */
    int  font;                 /* font index of its first cell (0 = the 16x32 UI text font) */
    int  rw, rh;               /* size in root cells (w, h scaled by the font) */
    int  hidden;               /* own flag; see cm_ui_visible for the inherited one */
} cm_ui_node;

typedef struct {
    int px, py;                /* window pixels */
    int cx, cy;                /* root cell */
    uint32_t hovered;          /* XConsole* the game tracks as hovered (0 = none) */
} cm_mouse;

typedef struct { uint32_t ch; uint8_t fg[3], bg[3]; } cm_cell;

static inline uint32_t cm_ui_r32(cm_read_fn rd, uint32_t a) { uint32_t v = 0; rd(a, &v, 4); return v; }

/* glyph size of game font `id` (fonts vector at *(ADDR_FONT_MGR) + FONTMGR_FONTS) */
static inline int cm_ui_font_size(cm_read_fn rd, int id, int *gw, int *gh) {
    uint32_t mgr = cm_ui_r32(rd, ADDR_FONT_MGR), v[2], wh[2];
    if (!mgr || id < 0 || !rd(mgr + FONTMGR_FONTS, v, 8) || v[1] <= v[0] || (uint32_t)id >= (v[1] - v[0]) / 4) return 0;
    uint32_t f = cm_ui_r32(rd, v[0] + 4 * (uint32_t)id);
    if (!f || !rd(f + XFONT_GLYPH_W, wh, 8) || !wh[0] || !wh[1] || wh[0] > 256 || wh[1] > 256) return 0;
    *gw = (int)wh[0]; *gh = (int)wh[1];
    return 1;
}

/* ".?AVCParts@@" -> "CParts"; cached per vtable */
static inline int cm_ui_class(cm_read_fn rd, uint32_t vt, char *out, size_t cap) {
    static struct { uint32_t vt; char name[40]; } cache[192];
    static int n;
    if (!cap) return 0;
    out[0] = 0;
    for (int i = 0; i < n; ++i) if (cache[i].vt == vt) { strncpy(out, cache[i].name, cap - 1); out[cap - 1] = 0; return 1; }
    uint32_t col = cm_ui_r32(rd, vt - 4), td = col ? cm_ui_r32(rd, col + 12) : 0;
    char raw[48];
    if (!td || !rd(td + 8, raw, sizeof raw)) return 0;
    raw[sizeof raw - 1] = 0;
    if (strncmp(raw, ".?AV", 4) && strncmp(raw, ".?AU", 4)) return 0;
    char *e = strstr(raw + 4, "@@");
    if (!e) return 0;
    *e = 0;
    raw[4 + 39] = 0;                                   /* class names fit cache[].name */
    if (n < (int)(sizeof cache / sizeof *cache)) { cache[n].vt = vt; memcpy(cache[n].name, raw + 4, 40); ++n; }
    size_t l = strlen(raw + 4);
    if (l >= cap) l = cap - 1;
    memcpy(out, raw + 4, l);
    out[l] = 0;
    return 1;
}

static inline int cm_ui_node_read(cm_read_fn rd, uint32_t a, cm_ui_node *n) {
    uint8_t b[CON_HIDDEN + 4];
    memset(n, 0, sizeof *n);
    if (!a || !rd(a, b, sizeof b)) return 0;
#define F(off) (*(int32_t *)(b + (off)))
    n->addr = a; n->vtable = (uint32_t)F(0); n->parent = (uint32_t)F(CON_PARENT);
    n->w = F(CON_W); n->h = F(CON_H); n->cells = (uint32_t)F(CON_CELLS);
    n->x = F(CON_X); n->y = F(CON_Y); n->ax = F(CON_ABS_X); n->ay = F(CON_ABS_Y);
    n->hidden = b[CON_HIDDEN] != 0;
    uint32_t kb = (uint32_t)F(CON_CHILDREN), ke = (uint32_t)F(CON_CHILDREN + 4);
#undef F
    if (n->w < 0 || n->h < 0 || n->w > 4096 || n->h > 4096) return 0;
    if (kb && ke >= kb && (ke - kb) / 4 < 4096) { n->kids = kb; n->nkids = (ke - kb) / 4; }
    if (!cm_ui_class(rd, n->vtable, n->cls, sizeof n->cls)) strcpy(n->cls, "?");
    n->rw = n->w; n->rh = n->h;
    uint32_t font = 0;
    int fw, fh, rw, rh;
    if (n->cells && n->w && n->h && rd(n->cells, &font, 4) && font && font < 64 &&
        cm_ui_font_size(rd, (int)font, &fw, &fh) && cm_ui_font_size(rd, 0, &rw, &rh)) {
        n->font = (int)font;
        n->rw = (n->w * fw + rw - 1) / rw;
        n->rh = (n->h * fh + rh - 1) / rh;
    }
    return 1;
}

/* the XRoot console, or 0 (also 0 if the pointer isn't an XRoot: before the UI exists, or a game update) */
static inline uint32_t cm_ui_root(cm_read_fn rd) {
    uint32_t r = cm_ui_r32(rd, ADDR_UI_ROOT);
    char cls[16];
    return r && cm_ui_class(rd, cm_ui_r32(rd, r), cls, sizeof cls) && !strcmp(cls, "XRoot") ? r : 0;
}
static inline uint32_t cm_ui_child(cm_read_fn rd, const cm_ui_node *n, uint32_t i) {
    return i < n->nkids ? cm_ui_r32(rd, n->kids + 4 * i) : 0;
}

/* visible = neither it nor any ancestor is hidden */
static inline int cm_ui_visible(cm_read_fn rd, uint32_t a) {
    for (int depth = 0; a && depth < 32; ++depth) {
        uint8_t h = 1;
        if (!rd(a + CON_HIDDEN, &h, 1) || h) return 0;
        a = cm_ui_r32(rd, a + CON_PARENT);
    }
    return 1;
}

static inline int cm_ui_mouse(cm_read_fn rd, cm_mouse *m) {
    uint32_t p = cm_ui_r32(rd, ADDR_MOUSE), v[13], wh[2];
    memset(m, 0, sizeof *m);
    uint32_t root = cm_ui_root(rd);                     /* no game UI (or not the game) -> no mouse */
    if (!root || !rd(root + CON_W, wh, 8) || !p || !rd(p, v, sizeof v)) return 0;
    if ((int32_t)v[MOUSE_CELL_X / 4] < -1 || (int32_t)v[MOUSE_CELL_X / 4] > (int32_t)wh[0] ||
        (int32_t)v[MOUSE_CELL_Y / 4] < -1 || (int32_t)v[MOUSE_CELL_Y / 4] > (int32_t)wh[1]) return 0;
    m->cx = (int32_t)v[MOUSE_CELL_X / 4]; m->cy = (int32_t)v[MOUSE_CELL_Y / 4];
    m->px = (int32_t)v[MOUSE_PX / 4];     m->py = (int32_t)v[MOUSE_PY / 4];
    m->hovered = v[MOUSE_HOVER / 4];
    return 1;
}

/* cell (x, y) of a console, console-local coordinates */
static inline int cm_ui_cell(cm_read_fn rd, const cm_ui_node *n, int x, int y, cm_cell *c) {
    uint8_t b[CELL_SIZE];
    if (!n->cells || x < 0 || y < 0 || x >= n->w || y >= n->h) return 0;
    if (!rd(n->cells + (uint32_t)(x * n->h + y) * CELL_SIZE, b, sizeof b)) return 0;
    c->ch = *(uint32_t *)(b + CELL_CHAR);
    memcpy(c->fg, b + CELL_FG, 3);
    memcpy(c->bg, b + CELL_BG, 3);
    return 1;
}

/* 0x80..0xff -> Unicode. 0x80-0x97 are Cogmind's own codes (its UI font sheet row 4): frames, 0x80-0x8a
 * verified by window-corner positions, 0x8b-0x97 by glyph shape. The rest: CP437 (0xb3 = │ checked). */
static const uint16_t CM_CP437_HI[128] = {
    0x2502,0x2500,0x253c,0x2524,0x2534,0x251c,0x252c,0x2514,0x250c,0x2510,0x2518,0x25a0,0x2551,0x2550,0x256c,0x2563,
    0x2569,0x2560,0x2566,0x255a,0x2554,0x2557,0x255d,0x25ae,0xff,0xd6,0xdc,0xa2,0xa3,0xa5,0x20a7,0x192,
    0xe1,0xed,0xf3,0xfa,0xf1,0xd1,0xaa,0xba,0xbf,0x2310,0xac,0xbd,0xbc,0xa1,0xab,0xbb,
    0x2591,0x2592,0x2593,0x2502,0x2524,0x2561,0x2562,0x2556,0x2555,0x2563,0x2551,0x2557,0x255d,0x255c,0x255b,0x2510,
    0x2514,0x2534,0x252c,0x251c,0x2500,0x253c,0x255e,0x255f,0x255a,0x2554,0x2569,0x2566,0x2560,0x2550,0x256c,0x2567,
    0x2568,0x2564,0x2565,0x2559,0x2558,0x2552,0x2553,0x256b,0x256a,0x2518,0x250c,0x2588,0x2584,0x258c,0x2590,0x2580,
    0x3b1,0xdf,0x393,0x3c0,0x3a3,0x3c3,0xb5,0x3c4,0x3a6,0x398,0x3a9,0x3b4,0x221e,0x3c6,0x3b5,0x2229,
    0x2261,0xb1,0x2265,0x2264,0x2320,0x2321,0xf7,0x2248,0xb0,0x2219,0xb7,0x221a,0x207f,0xb2,0x25a0,0xa0,
};

#define CM_UI_TILE 0xfffffffeu   /* a map/icon tile cell (tile fonts hold tile indices): shown as U+00B7 */

/* append one character as UTF-8; returns bytes written (0 if it doesn't fit) */
static inline size_t cm_ui_utf8(uint32_t ch, char *o, size_t room) {
    uint32_t u = ch == 0 ? ' ' : ch == CM_UI_TILE ? 0xb7 : ch < 0x20 ? '?' : ch < 0x80 ? ch : ch < 0x100 ? CM_CP437_HI[ch - 0x80] : '?';
    if (u < 0x80)  { if (room < 1) return 0; o[0] = (char)u; return 1; }
    if (u < 0x800) { if (room < 2) return 0; o[0] = (char)(0xc0 | u >> 6); o[1] = (char)(0x80 | (u & 0x3f)); return 2; }
    if (room < 3) return 0;
    o[0] = (char)(0xe0 | u >> 12); o[1] = (char)(0x80 | (u >> 6 & 0x3f)); o[2] = (char)(0x80 | (u & 0x3f));
    return 3;
}

/* chars of a cells column-major block: read column by column into a row-major char grid */
static inline int cm_ui_grid(cm_read_fn rd, uint32_t cells, int w, int h, int x0, int y0, int cw, int ch, uint32_t *grid) {
    static uint8_t col[CELL_SIZE * 512];
    static uint8_t blk[CELL_SIZE * 2048];
    if (!cells || cw <= 0 || ch <= 0 || ch > 512) return 0;
    /* full-height, in-bounds block: column-major makes it one contiguous read (rows: 1 read, not w) */
    if (y0 == 0 && ch == h && x0 >= 0 && x0 + cw <= w && cw * h <= 2048) {
        if (!rd(cells + (uint32_t)(x0 * h) * CELL_SIZE, blk, (size_t)(cw * h) * CELL_SIZE)) return 0;
        for (int x = 0; x < cw; ++x)
            for (int y = 0; y < h; ++y) {
                const uint8_t *c = blk + (size_t)(x * h + y) * CELL_SIZE;
                uint32_t chr = *(const uint32_t *)(c + CELL_CHAR), font = *(const uint32_t *)c;
                grid[y * cw + x] = font && chr > 0x7e ? CM_UI_TILE : chr;
            }
        return 1;
    }
    for (int x = 0; x < cw; ++x) {
        int cx = x0 + x;
        for (int y = 0; y < ch; ++y) grid[y * cw + x] = 0;
        if (cx < 0 || cx >= w) continue;
        int ya = y0 < 0 ? 0 : y0, yb = y0 + ch > h ? h : y0 + ch;
        if (yb <= ya) continue;
        if (!rd(cells + (uint32_t)(cx * h + ya) * CELL_SIZE, col, (size_t)(yb - ya) * CELL_SIZE)) return 0;
        for (int y = ya; y < yb; ++y) {
            const uint8_t *c = col + (y - ya) * CELL_SIZE;
            uint32_t chr = *(const uint32_t *)(c + CELL_CHAR), font = *(const uint32_t *)c;
            grid[(y - y0) * cw + x] = font && chr > 0x7e ? CM_UI_TILE : chr;   /* tile fonts: index, not text */
        }
    }
    return 1;
}

/* grid -> UTF-8 text, rows joined with '\n', trailing spaces trimmed; returns length */
static inline size_t cm_ui_grid_text(const uint32_t *grid, int cw, int ch, char *out, size_t cap) {
    size_t n = 0;
    if (!cap) return 0;
    for (int y = 0; y < ch; ++y) {
        if (y && n + 1 < cap) out[n++] = '\n';
        size_t row = n;
        for (int x = 0; x < cw; ++x) {
            size_t k = cm_ui_utf8(grid[y * cw + x], out + n, cap - 1 - n);
            if (!k) break;
            n += k;
        }
        while (n > row && out[n - 1] == ' ') --n;
    }
    out[n] = 0;
    return n;
}

/* row `y` of a console as text */
static inline size_t cm_ui_row_text(cm_read_fn rd, const cm_ui_node *n, int y, char *out, size_t cap) {
    static uint32_t grid[1024];
    int w = n->w > 1024 ? 1024 : n->w;
    if (cap) out[0] = 0;
    if (y < 0 || y >= n->h || !cm_ui_grid(rd, n->cells, n->w, n->h, 0, y, w, 1, grid)) return 0;
    return cm_ui_grid_text(grid, w, 1, out, cap);
}

/* all rows of a console (up to max_rows) as text */
static inline size_t cm_ui_text(cm_read_fn rd, const cm_ui_node *n, int max_rows, char *out, size_t cap) {
    static uint32_t grid[256 * 64];
    int w = n->w > 256 ? 256 : n->w, h = n->h > max_rows ? max_rows : n->h;
    if (h > 64) h = 64;
    if (cap) out[0] = 0;
    if (!cm_ui_grid(rd, n->cells, n->w, n->h, 0, 0, w, h, grid)) return 0;
    size_t len = cm_ui_grid_text(grid, w, h, out, cap);
    while (len && out[len - 1] == '\n') out[--len] = 0;
    return len;
}

/* a console's text with its visible children drawn over it (each child replaces its own area, later
 * children on top), in root cells. Unlike screen text it ignores whatever else is drawn there (other
 * windows, animated backgrounds), unlike cm_ui_text it includes labels/values held by children
 * (CInfoLine -> CInfoText). Tile-font consoles contribute nothing. */
static inline void cm_ui_compose(cm_read_fn rd, uint32_t a, int ox, int oy, int gw, int gh, uint32_t *grid, int depth) {
    static uint32_t own[2048];
    cm_ui_node n;
    if (depth > 16 || !cm_ui_node_read(rd, a, &n) || (depth && n.hidden)) return;
    /* own cells (UI-font consoles only), read in one go, copied where they overlap the region;
     * done before recursing, so the static buffer is free again for the children */
    if (!n.font && n.cells && n.w * n.h <= 2048 && cm_ui_grid(rd, n.cells, n.w, n.h, 0, 0, n.w, n.h, own))
        for (int y = 0; y < n.h; ++y) {
            int gy = n.ay - oy + y;
            if (gy < 0 || gy >= gh) continue;
            for (int x = 0; x < n.w; ++x) {
                int gx = n.ax - ox + x;
                if (gx >= 0 && gx < gw) grid[gy * gw + gx] = own[y * n.w + x];
            }
        }
    for (uint32_t i = 0; i < n.nkids; ++i) cm_ui_compose(rd, cm_ui_child(rd, &n, i), ox, oy, gw, gh, grid, depth + 1);
}
static inline size_t cm_ui_subtree_text(cm_read_fn rd, const cm_ui_node *n, char *out, size_t cap) {
    static uint32_t grid[256 * 64];
    int w = n->rw > 256 ? 256 : n->rw, h = n->rh > 64 ? 64 : n->rh;
    if (cap) out[0] = 0;
    if (w <= 0 || h <= 0) return 0;
    for (int i = 0; i < w * h; ++i) grid[i] = ' ';
    cm_ui_compose(rd, n->addr, n->ax, n->ay, w, h, grid, 0);
    size_t len = cm_ui_grid_text(grid, w, h, out, cap);
    while (len && out[len - 1] == '\n') out[--len] = 0;
    return len;
}

/* a region of the composited screen (XRoot's grid), root cells */
static inline size_t cm_screen_text(cm_read_fn rd, int x, int y, int w, int h, char *out, size_t cap) {
    static uint32_t grid[512 * 128];
    cm_ui_node r;
    if (cap) out[0] = 0;
    if (!cm_ui_node_read(rd, cm_ui_root(rd), &r)) return 0;
    if (w <= 0 || x + w > r.w) w = r.w - x;
    if (h <= 0 || y + h > r.h) h = r.h - y;
    if (w > 512) w = 512;
    if (h > 128) h = 128;
    if (x < 0 || y < 0 || w <= 0 || h <= 0 || !cm_ui_grid(rd, r.cells, r.w, r.h, x, y, w, h, grid)) return 0;
    return cm_ui_grid_text(grid, w, h, out, cap);
}

/* depth-first search: first node of class `cls` (visible ones only if visible_only) */
static inline uint32_t cm_ui_find_from(cm_read_fn rd, uint32_t a, const char *cls, int visible_only, int depth) {
    cm_ui_node n;
    if (depth > 24 || !cm_ui_node_read(rd, a, &n) || (visible_only && n.hidden)) return 0;
    if (!strcmp(n.cls, cls)) return a;
    for (uint32_t i = 0; i < n.nkids; ++i) {
        uint32_t r = cm_ui_find_from(rd, cm_ui_child(rd, &n, i), cls, visible_only, depth + 1);
        if (r) return r;
    }
    return 0;
}
static inline uint32_t cm_ui_find(cm_read_fn rd, uint32_t root, const char *cls, int visible_only) {
    return cm_ui_find_from(rd, root, cls, visible_only, 0);
}

/* deepest visible node whose bounds contain root cell (cx, cy); later siblings are on top */
static inline uint32_t cm_ui_at_from(cm_read_fn rd, uint32_t a, int cx, int cy, int depth) {
    cm_ui_node n;
    if (depth > 24 || !cm_ui_node_read(rd, a, &n) || n.hidden) return 0;
    for (uint32_t i = n.nkids; i-- > 0;) {
        uint32_t r = cm_ui_at_from(rd, cm_ui_child(rd, &n, i), cx, cy, depth + 1);
        if (r) return r;
    }
    /* 1x1 containers (CMission, CEffects...) hold children but don't own their area */
    if (n.w > 1 && n.h > 0 && cx >= n.ax && cy >= n.ay && cx < n.ax + n.rw && cy < n.ay + n.rh) return a;
    return 0;
}
static inline uint32_t cm_ui_at(cm_read_fn rd, int cx, int cy) {
    return cm_ui_at_from(rd, cm_ui_root(rd), cx, cy, 0);
}

/* world map tile under root cell (cx, cy), if it is inside the visible map view (CMap): its cells are
 * map tiles in the map font (64x64 or 32x32 when zoomed), CMap +0x6c/+0x70 hold the view offset. */
static inline int cm_ui_map_tile(cm_read_fn rd, int cx, int cy, int *wx, int *wy) {
    cm_ui_node n;
    int fw, fh, rw, rh;
    int32_t off[2];
    if (!cm_ui_node_read(rd, cm_ui_find(rd, cm_ui_root(rd), "CMap", 1), &n) || !cm_ui_visible(rd, n.addr)) return 0;
    if (cx < n.ax || cy < n.ay || cx >= n.ax + n.rw || cy >= n.ay + n.rh) return 0;
    if (!cm_ui_font_size(rd, n.font, &fw, &fh) || !cm_ui_font_size(rd, 0, &rw, &rh) || !rd(n.addr + CMAP_VIEW_X, off, 8)) return 0;
    int tx = (cx - n.ax) * rw / fw, ty = (cy - n.ay) * rh / fh;
    if (tx >= n.w || ty >= n.h) return 0;
    *wx = tx - off[0]; *wy = ty - off[1];
    return 1;
}

/* one-line description (bounds in root cells): "CPart 156,15 56x1: A . Rnf. Deuterium Engine" */
static inline size_t cm_ui_describe(cm_read_fn rd, uint32_t a, char *out, size_t cap) {
    cm_ui_node n;
    char text[256] = "";
    if (cap) out[0] = 0;
    if (!cm_ui_node_read(rd, a, &n)) return 0;
    cm_ui_row_text(rd, &n, 0, text, sizeof text);
    const char *t = text;
    while (*t == ' ') ++t;
    int len = snprintf(out, cap, "%s %d,%d %dx%d%s%s", n.cls, n.ax, n.ay, n.rw, n.rh, *t ? ": " : "", t);
    return len < 0 ? 0 : (size_t)len < cap ? (size_t)len : cap - 1;
}

#endif
