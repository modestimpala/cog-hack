// SPDX-License-Identifier: MPL-2.0
// Plugin UI events (api->on_ui): hover changes, polled every SDL_PollEvent from the game's own
// hover pointer, and mouse button events, seen in SDL_PollEvent before the game gets them.
// A plugin returning 1 for MOUSE_DOWN swallows the click and its matching MOUSE_UP.
#include <string.h>
#include "cogmind.h"
#include "cm_ui.h"
#include "base/mem.h"
#include "hook/ui_events.h"

#define MAX_UI_FN 16
static struct { cm_ui_fn fn; void *user; } g_fn[MAX_UI_FN];
static int g_nfn;
static uint32_t g_hover;
static int g_swallow_up = -1;     // button whose MOUSE_UP we swallow (its DOWN was consumed)

int ui_event_register(cm_ui_fn fn, void *user) {
    if (!fn || g_nfn >= MAX_UI_FN) return -1;
    g_fn[g_nfn].fn = fn; g_fn[g_nfn].user = user; ++g_nfn;
    return 0;
}

static int dispatch(const cm_ui_event *e) {
    int eaten = 0;
    for (int i = 0; i < g_nfn; ++i) eaten |= g_fn[i].fn(g_fn[i].user, e) == 1;
    return eaten;
}

// pixel -> root cell, the game's own mapping (FUN_00418b50: (px - origin) / glyph size of font 0)
static void px_to_cell(int px, int py, int *cx, int *cy) {
    int gw = 16, gh = 32, ox = 0, oy = 0;
    if (cm_ui_font_size(rd, 0, &gw, &gh)) {          // no fonts: no game display (tests), origin 0
        ox = rdi(ADDR_DISPLAY + DISPLAY_ORIGIN_X);
        oy = rdi(ADDR_DISPLAY + DISPLAY_ORIGIN_Y);
    }
    *cx = (px - ox) / gw;
    *cy = (py - oy) / gh;
}

void ui_events_tick(void) {
    if (!g_nfn) return;
    uint32_t m = rd32(ADDR_MOUSE), h = m ? rd32(m + MOUSE_HOVER) : 0;
    if (h == g_hover) return;
    cm_ui_event e;
    memset(&e, 0, sizeof e);
    cm_mouse ms;
    if (cm_ui_mouse(rd, &ms)) { e.px = ms.px; e.py = ms.py; e.cx = ms.cx; e.cy = ms.cy; }
    e.type = CM_UI_HOVER; e.node = h; e.prev = g_hover;
    g_hover = h;
    dispatch(&e);
}

// SDL 1.2 SDL_MouseButtonEvent: +0 type (5 down, 6 up), +2 button, +4 x (u16), +6 y (u16)
int ui_events_mouse(const uint8_t *ev) {
    if (!g_nfn || (ev[0] != 5 && ev[0] != 6)) return 0;
    cm_ui_event e;
    memset(&e, 0, sizeof e);
    e.type = ev[0] == 5 ? CM_UI_MOUSE_DOWN : CM_UI_MOUSE_UP;
    e.button = ev[2];
    e.px = *(const uint16_t *)(ev + 4); e.py = *(const uint16_t *)(ev + 6);
    px_to_cell(e.px, e.py, &e.cx, &e.cy);
    e.node = cm_ui_at(rd, e.cx, e.cy);
    int eaten = dispatch(&e);
    if (e.type == CM_UI_MOUSE_DOWN) { if (eaten) g_swallow_up = e.button; return eaten; }
    if (e.button == g_swallow_up) { g_swallow_up = -1; return 1; }
    return 0;
}
