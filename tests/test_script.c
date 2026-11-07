// SPDX-License-Identifier: MPL-2.0
// Offline test driver: runs the AngelScript layer + plugin loader outside the game.
// Game reads fail safely (no game memory here), so this exercises everything except real game state.
// Run from a directory containing hack/ (tests/run.sh builds one). Output is checked by run.sh.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <windows.h>
#include "cogmind.h"
#include "base/config.h"
#include "debug/crash.h"
#include "game/player.h"
#include "hook/input.h"
#include "hook/ui_events.h"
#include "overlay/panels.h"
#include "script/plugin_host.h"
#include "script/script.h"

int main(int argc, char **argv) {
    config_load("hack\\config.txt");
    // as if the startup scan had found the World pointer moved: plugins must see the host's value
    cm_addr_table[CM_A_ADDR_WORLD_PTR] = 0x00cf0d2c;
    script_init();
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--frame")) { script_frame(); continue; }
        if (!strcmp(argv[i], "--click") && i + 2 < argc) {   // left mouse down at root cell x, y (16x32 cells, no game)
            unsigned char ev[20] = { 5, 0, 1 };
            *(unsigned short *)(ev + 4) = (unsigned short)(atoi(argv[i + 1]) * 16 + 1);
            *(unsigned short *)(ev + 6) = (unsigned short)(atoi(argv[i + 2]) * 32 + 1);
            i += 2;
            printf("swallowed=%d\n", ui_events_mouse(ev));
            continue;
        }
        if (!strcmp(argv[i], "--crash")) {          // crash report: a null write while "our" code runs
            SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
            crash_init();
            crash_note_command(18);
            crash_phase("test");
            fflush(stdout);
            *(volatile int *)(uintptr_t)argc = 0;   // argc is small: an address in the null page
            continue;
        }
        if (!strcmp(argv[i], "--ticks") && i + 1 < argc) {   // pump plugin ticks for N ms (pipe tests)
            DWORD until = GetTickCount() + (DWORD)atoi(argv[++i]);
            while ((int)(until - GetTickCount()) > 0) { game_tick(); plugins_tick(); Sleep(2); }
            continue;
        }
        if (!strcmp(argv[i], "--panels")) {
            for (int p = 0; p < panel_count(); ++p) if (panel_text(p)) printf("panel %d: %s\n", p, panel_text(p));
            continue;
        }
        if (!strcmp(argv[i], "--hotkey") && i + 1 < argc) {   // config hotkey parser
            int sym = 0, mods = 0, ok = key_parse(argv[++i], &sym, &mods);
            printf("hotkey '%s' -> %s sym=%d mods=%d\n", argv[i], ok ? "ok" : "BAD", sym, mods);
            continue;
        }
        if (!strcmp(argv[i], "--synth")) {          // drain synthetic key queue
            unsigned char ev[20];
            while (input_pop(ev)) printf("synth type=%d sym=%d mod=%x\n", ev[0], *(int *)(ev + 8), *(int *)(ev + 12));
            continue;
        }
        printf("> %s\n", argv[i]);
        fflush(stdout);
        script_exec_for_test(argv[i]);
    }
    return 0;
}
