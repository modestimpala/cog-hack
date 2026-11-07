// SPDX-License-Identifier: MPL-2.0
// Overlay text panels set by plugins (api->overlay_set) and drawn by overlay.c. Main thread only.
#include <string.h>
#include "overlay/panels.h"

#define MAX_PANELS 4
static struct { char id[24]; char text[1024]; } g_panels[MAX_PANELS];

void panel_set(const char *id, const char *text) {
    if (!id || !*id) return;
    int slot = -1, empty = -1;
    for (int i = 0; i < MAX_PANELS; ++i) {
        if (!strcmp(g_panels[i].id, id)) slot = i;
        else if (!g_panels[i].id[0] && empty < 0) empty = i;
    }
    if (!text || !*text) { if (slot >= 0) g_panels[slot].id[0] = g_panels[slot].text[0] = 0; return; }
    if (slot < 0) slot = empty;
    if (slot < 0) return;                                   // all panels taken
    strncpy(g_panels[slot].id, id, sizeof g_panels[slot].id - 1);
    strncpy(g_panels[slot].text, text, sizeof g_panels[slot].text - 1);
    g_panels[slot].text[sizeof g_panels[slot].text - 1] = 0;
}

const char *panel_text(int i) {
    return i >= 0 && i < MAX_PANELS && g_panels[i].id[0] ? g_panels[i].text : NULL;
}
int panel_count(void) { return MAX_PANELS; }

// ---- labels: text anchored to a root UI cell (api->label_set / script ui_label) ----
#define MAX_LABELS 32
static ui_label g_labels[MAX_LABELS];

void label_set(const char *id, int cx, int cy, const char *text, uint32_t fg, uint32_t bg) {
    if (!id || !*id) return;
    int slot = -1, empty = -1;
    for (int i = 0; i < MAX_LABELS; ++i) {
        if (!strcmp(g_labels[i].id, id)) slot = i;
        else if (!g_labels[i].id[0] && empty < 0) empty = i;
    }
    if (!text || !*text) { if (slot >= 0) memset(&g_labels[slot], 0, sizeof g_labels[slot]); return; }
    if (slot < 0) slot = empty;
    if (slot < 0) return;
    ui_label *l = &g_labels[slot];
    l->tile_font = -1;
    strncpy(l->id, id, sizeof l->id - 1);
    strncpy(l->text, text, sizeof l->text - 1);
    l->text[sizeof l->text - 1] = 0;
    l->cx = cx; l->cy = cy; l->fg = fg; l->bg = bg;
}
const ui_label *label_get(int i) { return i >= 0 && i < MAX_LABELS && g_labels[i].id[0] ? &g_labels[i] : NULL; }
int label_count(void) { return MAX_LABELS; }

void label_set_tile(const char *id, int cx, int cy, int font, int glyph, uint32_t tile_fg,
                    const char *text, uint32_t fg, uint32_t bg) {
    label_set(id, cx, cy, text && *text ? text : " ", fg, bg);   // " ": a tile-only label still exists
    for (int i = 0; i < MAX_LABELS; ++i)
        if (!strcmp(g_labels[i].id, id)) {
            if (!text || !*text) g_labels[i].text[0] = 0;
            g_labels[i].tile_font = font; g_labels[i].tile_glyph = glyph; g_labels[i].tile_fg = tile_fg;
        }
}
