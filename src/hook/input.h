// SPDX-License-Identifier: MPL-2.0
// Synthetic key input and key names.
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
void inject_key(int sdl_sym, int sdl_mod);   // queue a key press + release
int  input_pop(void *ev);                    // 1 if a queued event was copied into ev (SDL_Event)
enum { HK_CTRL = 1, HK_SHIFT = 2, HK_ALT = 4 };
int  key_parse(const char *spec, int *sym, int *mods);   // "ctrl+shift+f11" -> SDL sym + HK_* set; 1 ok
#ifdef __cplusplus
}
#endif
