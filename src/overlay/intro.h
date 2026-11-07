// SPDX-License-Identifier: MPL-2.0
// A cog-hack line in the new-game boot sequence (config.txt intro_*).
#pragma once
#include "hook/sdl.h"
int intro_draw(SDL_Surface *s);   // call once per presented frame; 1 if it drew (the caller restores the backing)
