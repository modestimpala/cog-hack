// SPDX-License-Identifier: MPL-2.0
// Minimal SDL 1.2 (32-bit) layout: just what the proxy needs. Real SDL lives in SDL_orig.dll.
#pragma once
#include <stdint.h>

typedef struct { int16_t x, y; uint16_t w, h; } SDL_Rect;
typedef struct {
    void    *palette;                            // 0
    uint8_t  BitsPerPixel, BytesPerPixel;        // 4, 5
    uint8_t  Rloss, Gloss, Bloss, Aloss;         // 6..9
    uint8_t  Rshift, Gshift, Bshift, Ashift;     // 10..13
    uint32_t Rmask, Gmask, Bmask, Amask;         // 16..31
} SDL_PixelFormat;
typedef struct SDL_Surface {
    uint32_t flags;      // 0
    SDL_PixelFormat *format;   // 4
    int      w, h;       // 8, 12
    uint16_t pitch;      // 16
    void    *pixels;     // 20
} SDL_Surface;

// SDL_Event is a 20-byte union (verified: SDL_PeepEvents indexes its queue by idx*20).
// Keyboard event: +0 type(u8) +2 state(u8) +4 scancode(u8) +8 sym(i32) +12 mod(i32) +16 unicode(u16)
#define SDL_EVENT_SIZE 20
#define SDL_KEYDOWN 2
#define SDL_KEYUP   3

#define SDLK_PAUSE  19
#define SDLK_F9     290
#define SDLK_F10    291
#define SDLK_F11    292
#define SDLK_F12    293
#define KMOD_CTRL   0x00C0
#define KMOD_SHIFT  0x0003
#define KMOD_ALT    0x0300

typedef int          (__cdecl *PollEvent_t)(void *ev);
typedef int          (__cdecl *Flip_t)(SDL_Surface *s);
typedef void         (__cdecl *UpdateRect_t)(SDL_Surface *s, int32_t x, int32_t y, uint32_t w, uint32_t h);
typedef SDL_Surface *(__cdecl *SetVideoMode_t)(int w, int h, int bpp, uint32_t flags);
typedef int          (__cdecl *FillRect_t)(SDL_Surface *s, SDL_Rect *r, uint32_t color);
typedef uint32_t     (__cdecl *MapRGB_t)(void *fmt, uint8_t r, uint8_t g, uint8_t b);

extern FillRect_t real_FillRect;
extern MapRGB_t   real_MapRGB;
