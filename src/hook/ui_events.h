// SPDX-License-Identifier: MPL-2.0
// Plugin UI events: hover changes (polled from the game's hover pointer) and mouse clicks
// (seen in SDL_PollEvent before the game gets them).
#pragma once
#include <stdint.h>
#include "plugin_api.h"
#ifdef __cplusplus
extern "C" {
#endif
int  ui_event_register(cm_ui_fn fn, void *user);
void ui_events_tick(void);                // hover-change detection, every poll
int  ui_events_mouse(const uint8_t *ev);  // an SDL event; 1 = a plugin swallowed it
#ifdef __cplusplus
}
#endif
