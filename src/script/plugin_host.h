// SPDX-License-Identifier: MPL-2.0
// Native plugins: the loader and the host API they get (include/plugin_api.h).
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
void plugins_tick(void);           // main thread, every SDL_PollEvent: plugin on_tick callbacks
#ifdef __cplusplus
}
#endif
