// SPDX-License-Identifier: MPL-2.0
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
void script_init(void);            // main thread, once
void script_pump(void);            // main thread, every SDL_PollEvent: runs queued console lines
void script_toggle_console(void);  // Ctrl+F9
void script_frame(void);           // once per presented frame (onFrame)
int  script_exec_for_test(const char *line);
#ifdef __cplusplus
}
#endif
