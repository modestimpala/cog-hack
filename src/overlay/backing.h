// SPDX-License-Identifier: MPL-2.0
// Saves the pixels under what the overlay draws and puts them back after the frame is presented.
#pragma once
#include "hook/sdl.h"
void backing_save(SDL_Surface *s, int x, int y, int w, int h);   // call before drawing into the rect
void backing_restore(SDL_Surface *s);                            // after presenting: undo all drawing
