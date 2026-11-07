// SPDX-License-Identifier: MPL-2.0
// Debug captures into hack\snapshots (hotkeys).
#pragma once
void snapshot_write(void);           // Ctrl+F11: memory snapshot + screen text + screen bmp
void ui_capture(void);               // Ctrl+Shift+F11: screen text, hover and UI tree only
void state_dump(void);               // Ctrl+F10: text dump of player, items, bots
void dump_state(const char *path);   // the Ctrl+F10 text, to a file
int  screen_bmp(const char *path);   // the screen surface as a .bmp; 1 ok
