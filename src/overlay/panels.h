// SPDX-License-Identifier: MPL-2.0
// Plugin text panels (drawn in a corner) and labels (text anchored to a root UI cell).
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void        panel_set(const char *id, const char *text);   // "" removes
const char *panel_text(int i);                              // NULL if slot i is unused
int         panel_count(void);

typedef struct { char id[24]; char text[2048]; int cx, cy; uint32_t fg, bg; int tile_font, tile_glyph; uint32_t tile_fg; } ui_label;
#define LABEL_NO_BG 0xff000000u
void label_set(const char *id, int cx, int cy, const char *text, uint32_t fg, uint32_t bg);   // "" removes
// a label that starts with a game tile (glyph `glyph` of game font `font`, tinted tile_fg), then text;
// removed with label_set(id, .., "", ..)
void label_set_tile(const char *id, int cx, int cy, int font, int glyph, uint32_t tile_fg,
                    const char *text, uint32_t fg, uint32_t bg);
const ui_label *label_get(int i);                           // NULL if slot i is unused
int             label_count(void);
#ifdef __cplusplus
}
#endif
