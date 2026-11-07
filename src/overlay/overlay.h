// SPDX-License-Identifier: MPL-2.0
// The overlay: stats box, plugin panels and labels, drawn onto the game's screen surface just
// before each frame is presented.
#pragma once
#include "hook/sdl.h"
void overlay_init(int visible);
void overlay_toggle(void);
int  overlay_visible(void);
void overlay_draw(SDL_Surface *s);
void overlay_restore(SDL_Surface *s);   // after presenting: put back the pixels the overlay covered
