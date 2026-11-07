// SPDX-License-Identifier: MPL-2.0
// Our hotkeys (config.txt hotkey_*).
#pragma once
#include <stdint.h>
void hotkeys_load(void);
int  hotkeys_handle(const uint8_t *sdl_event);   // 1 = ours, the game never sees it (nor its key-up)
