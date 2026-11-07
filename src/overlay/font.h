// SPDX-License-Identifier: MPL-2.0
// The game's fonts and a built-in pixel font, drawn onto a 32-bit SDL surface.
#pragma once
#include <stdint.h>
#include "hook/sdl.h"
#define FONT_PX_SCALE 3     // built-in font: 3x5 glyphs, each pixel drawn SCALE x SCALE
typedef struct { const uint8_t *pixels; int pitch, gw, gh, ashift, cols, rows; } game_font;
void font_init(void);
// Game font idx (white glyphs, shape in alpha). Re-validate on every use: the game rebuilds fonts
// on mode changes. 1 ok.
int  font_load(int idx, game_font *out);
void font_blit_glyph(SDL_Surface *s, int x, int y, const game_font *gf, int glyph, uint32_t rgb);
// UTF-8 text; box drawing characters use the game's frame glyphs
void font_draw(SDL_Surface *s, int x, int y, const game_font *gf, const char *str, uint32_t rgb);
int  font_text_cells(const char *str);   // characters in UTF-8 text = cells it takes
void font_px_draw(SDL_Surface *s, int x, int y, const char *str, uint32_t mapped_color);
