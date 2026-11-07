// SPDX-License-Identifier: MPL-2.0
// COG-HACK on the title screen (config.txt title_art).
#pragma once
#include "hook/sdl.h"
int title_draw(SDL_Surface *s);   // call once per presented frame; 1 if it drew (the caller restores the backing)
