// SPDX-License-Identifier: MPL-2.0
// Game commands: the same path the game's key bindings take.
#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
int command_find(const char *name);              // id or -1
int command_name(int id, char *out, size_t cap);
int game_command(int id);                        // 1 handled, 0 not handled, -1 error
int game_command_at(int id, int x, int y);       // mouse-style command with a position
int install_command_observer(void (*fn)(void *cmd));
#ifdef __cplusplus
}
#endif
