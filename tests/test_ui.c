// SPDX-License-Identifier: MPL-2.0
// Unit test for include/cm_ui.h over a hand-built address space laid out like the game's
// (offsets from cogmind.h, structures as found in the game): RTTI class names,
// column-major cell grids, per-font console sizes, hidden flags, the mouse/hover object.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cm_ui.h"

// two fake regions: game statics / vtables / RTTI, and a heap
#define IMG_BASE 0x00c00000u
#define IMG_SIZE 0x00180000u
#define HEAP_BASE 0x10000000u
#define HEAP_SIZE 0x00400000u
static uint8_t *g_img, *g_heap;
static uint32_t g_brk = HEAP_BASE, g_rbrk = 0x00c40000u;

static uint8_t *at(uint32_t a, size_t n) {
    if (a >= IMG_BASE && a + n <= IMG_BASE + IMG_SIZE) return g_img + (a - IMG_BASE);
    if (a >= HEAP_BASE && a + n <= HEAP_BASE + HEAP_SIZE) return g_heap + (a - HEAP_BASE);
    return NULL;
}
static int __cdecl rd(uint32_t a, void *o, size_t n) { uint8_t *p = at(a, n); if (!p) return 0; memcpy(o, p, n); return 1; }
static void w32(uint32_t a, uint32_t v) { memcpy(at(a, 4), &v, 4); }
static uint32_t halloc(size_t n) { uint32_t a = g_brk; g_brk += (uint32_t)((n + 15) & ~15u); return a; }

// a vtable whose RTTI names `cls`
static uint32_t vtable(const char *cls) {
    uint32_t col = g_rbrk, td = g_rbrk + 0x20, vt = g_rbrk + 0x84;
    w32(col + 12, td);
    char name[48];
    snprintf(name, sizeof name, ".?AV%s@@", cls);
    memcpy(at(td + 8, sizeof name), name, sizeof name);
    w32(vt - 4, col);
    g_rbrk += 0x100;
    return vt;
}

// console with its own grid (column-major), font id in every cell's +0
static uint32_t console(const char *cls, uint32_t parent, int ax, int ay, int w, int h, int font, int hidden) {
    uint32_t c = halloc(0x80), cells = halloc((size_t)w * h * CELL_SIZE);
    w32(c, vtable(cls)); w32(c + CON_PARENT, parent);
    w32(c + CON_W, (uint32_t)w); w32(c + CON_H, (uint32_t)h); w32(c + CON_CELLS, cells);
    w32(c + CON_ABS_X, (uint32_t)ax); w32(c + CON_ABS_Y, (uint32_t)ay);
    *at(c + CON_HIDDEN, 1) = (uint8_t)hidden;
    for (int i = 0; i < w * h; ++i) { w32(cells + (uint32_t)i * CELL_SIZE, (uint32_t)font); w32(cells + (uint32_t)i * CELL_SIZE + CELL_CHAR, ' '); }
    if (parent) {                                   // append to the parent's children vector
        uint32_t b = 0, e = 0;
        rd(parent + CON_CHILDREN, &b, 4); rd(parent + CON_CHILDREN + 4, &e, 4);
        if (!b) { b = e = halloc(64 * 4); w32(parent + CON_CHILDREN, b); }
        w32(e, c); w32(parent + CON_CHILDREN + 4, e + 4);
    }
    return c;
}
static void put(uint32_t c, int x, int y, uint32_t ch) {
    uint32_t h = 0, cells = 0;
    rd(c + CON_H, &h, 4); rd(c + CON_CELLS, &cells, 4);
    w32(cells + (uint32_t)(x * (int)h + y) * CELL_SIZE + CELL_CHAR, ch);
}
static void puts_at(uint32_t c, int x, int y, const char *s) { for (; *s; ++s, ++x) put(c, x, y, (unsigned char)*s); }

static int fails;
static void check(int ok, const char *what) { printf("  %s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++fails; }
static void check_str(const char *got, const char *want, const char *what) {
    int ok = !strcmp(got, want);
    printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) { printf("       got  [%s]\n       want [%s]\n", got, want); ++fails; }
}

int main(void) {
    g_img = calloc(1, IMG_SIZE); g_heap = calloc(1, HEAP_SIZE);

    // fonts: 0 = 16x32 UI text, 3 = 64x64 map tiles
    uint32_t mgr = halloc(0x40), fv = halloc(4 * 4);
    for (int i = 0; i < 4; ++i) {
        uint32_t f = halloc(0x40);
        w32(f + XFONT_GLYPH_W, i == 3 ? 64 : 16); w32(f + XFONT_GLYPH_H, i == 3 ? 64 : 32);
        w32(fv + 4 * (uint32_t)i, f);
    }
    w32(mgr + FONTMGR_FONTS, fv); w32(mgr + FONTMGR_FONTS + 4, fv + 16);
    w32(ADDR_FONT_MGR, mgr);

    // XRoot 214x45 -> CMission (1x1 container) -> CLog, CMap (64px tiles), CHud -> CHudData, hidden CInfo
    uint32_t root = console("XRoot", 0, 0, 0, 214, 45, 0, 0);
    w32(ADDR_UI_ROOT, root);
    uint32_t mission = console("CMission", root, 0, 0, 1, 1, 0, 0);
    uint32_t log = console("CLog", mission, 0, 0, 77, 10, 0, 0);
    uint32_t map = console("CMap", mission, 0, 10, 38, 17, 3, 0);
    uint32_t hud = console("CHud", mission, 154, 0, 60, 9, 0, 0);
    uint32_t core = console("CHudData", hud, 154, 1, 60, 1, 0, 0);
    uint32_t info = console("CInfo", mission, 0, 0, 50, 45, 0, 1);
    uint32_t title = console("ConsoleTitle", info, 18, 0, 15, 1, 0, 0);
    puts_at(core, 0, 0, " CORE   80/700 (21% exposed)");
    puts_at(log, 0, 1, "multi");
    puts_at(log, 0, 2, "row");
    // the composited screen: a frame top-left, text, a map tile, and the HUD line
    put(root, 0, 0, 0x88); put(root, 1, 0, 0x81); puts_at(root, 2, 0, "/ L O G /"); put(root, 11, 0, 0x89);
    put(root, 0, 1, 0x80); puts_at(root, 1, 1, "00042_ ALERT");
    w32(*(uint32_t *)at(root + CON_CELLS, 4) + (uint32_t)(5 * 45 + 12) * CELL_SIZE, 3);           // font 3 tile
    put(root, 5, 12, 0x1e5);
    puts_at(root, 154, 1, " CORE   80/700");

    // mouse: cell 84,20 over the map; the game's hovered = CMap
    uint32_t mouse = halloc(0x40);
    w32(mouse + MOUSE_CELL_X, 84); w32(mouse + MOUSE_CELL_Y, 20);
    w32(mouse + MOUSE_PX, 1367); w32(mouse + MOUSE_PY, 671); w32(mouse + MOUSE_HOVER, map);
    w32(ADDR_MOUSE, mouse);

    puts("cm_ui.h:");
    cm_ui_node n;
    char b[1024];
    check(cm_ui_node_read(rd, cm_ui_root(rd), &n) && !strcmp(n.cls, "XRoot") && n.w == 214 && n.h == 45, "root: XRoot 214x45 via RTTI");
    check(cm_ui_node_read(rd, map, &n) && n.font == 3 && n.rw == 152 && n.rh == 34, "CMap 38x17 in 64px tiles = 152x34 root cells");
    check(cm_ui_find(rd, root, "CHudData", 1) == core, "find CHudData (through the 1x1 container)");
    check(cm_ui_find(rd, root, "ConsoleTitle", 1) == 0, "find skips children of a hidden window");
    check(cm_ui_find(rd, root, "ConsoleTitle", 0) == title, "find with visible_only=0 sees them");
    check(!cm_ui_visible(rd, title) && cm_ui_visible(rd, core), "visible: inherited hidden flag");
    check(cm_ui_at(rd, 84, 20) == map, "at(84,20) = CMap (scaled bounds)");
    check(cm_ui_at(rd, 160, 1) == core, "at(160,1) = the HUD row, not CHud");
    check(cm_ui_at(rd, 30, 5) == log, "at(30,5) = CLog: the hidden CInfo on top of it is skipped");
    check(cm_ui_at(rd, 100, 5) == root, "at(100,5): nothing there but the root");
    cm_ui_node_read(rd, core, &n);
    cm_ui_row_text(rd, &n, 0, b, sizeof b);
    check_str(b, " CORE   80/700 (21% exposed)", "row text (trailing spaces trimmed)");
    cm_ui_node_read(rd, log, &n);
    cm_ui_text(rd, &n, 64, b, sizeof b);
    check_str(b, "\nmulti\nrow", "multi-row console text is column-major");
    cm_screen_text(rd, 0, 0, 14, 2, b, sizeof b);
    check_str(b, "\xe2\x94\x8c\xe2\x94\x80/ L O G /\xe2\x94\x90\n\xe2\x94\x82" "00042_ ALERT", "screen text: frame glyphs as box drawing");
    cm_screen_text(rd, 4, 12, 3, 1, b, sizeof b);
    check_str(b, " \xc2\xb7", "screen text: map tile as a middle dot");
    cm_screen_text(rd, 154, 1, 60, 1, b, sizeof b);
    check_str(b, " CORE   80/700", "screen text: HUD region");
    w32(map + CMAP_VIEW_X, (uint32_t)-22); w32(map + CMAP_VIEW_Y, (uint32_t)-102);
    int wx = 0, wy = 0;
    check(cm_ui_map_tile(rd, 84, 20, &wx, &wy) && wx == 21 + 22 && wy == 5 + 102, "map tile under cell 84,20 = tile 21,5 + view 22,102");
    check(!cm_ui_map_tile(rd, 160, 1, &wx, &wy), "no map tile over the HUD");
    uint32_t line = console("CInfoLine", info, 1, 5, 20, 1, 0, 0), lab = console("CInfoText", line, 3, 5, 9, 1, 0, 0);
    puts_at(line, 0, 0, "      ##############");
    puts_at(lab, 0, 0, "Integrity");
    cm_ui_node_read(rd, line, &n);
    cm_ui_subtree_text(rd, &n, b, sizeof b);
    check_str(b, "  Integrity#########", "subtree text: child label drawn over the line's own cells");
    cm_mouse m;
    check(cm_ui_mouse(rd, &m) && m.cx == 84 && m.cy == 20 && m.px == 1367 && m.hovered == map, "mouse cell/pixels/hovered");
    cm_ui_describe(rd, core, b, sizeof b);
    check_str(b, "CHudData 154,1 60x1: CORE   80/700 (21% exposed)", "describe");
    check(cm_ui_describe(rd, 0, b, sizeof b) == 0 && !b[0], "describe(0) is empty");
    check(cm_screen_text(rd, 0, 0, 0, 0, b, sizeof b) > 0 && strchr(b, '\n') != NULL, "whole screen");
    return fails ? 1 : 0;
}
