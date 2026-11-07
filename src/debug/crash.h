// SPDX-License-Identifier: MPL-2.0
// Crash reports: a vectored exception handler that logs a fault and writes a minidump.
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void        crash_init(void);
const char *crash_phase(const char *phase);   // what is running now ("game" = not our code); returns the previous one
void        crash_note_command(int id);       // the game is about to run this command
void        crash_note_input(const uint8_t *sdl_event);   // key and mouse presses
#ifdef __cplusplus
}
#endif
