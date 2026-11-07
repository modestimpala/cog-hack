// SPDX-License-Identifier: MPL-2.0
// Our hotkeys, read from config.txt and checked on every SDL event before the game sees it.
#include <stdint.h>
#include <string.h>
#include "base/config.h"
#include "base/log.h"
#include "debug/record.h"
#include "debug/snapshot.h"
#include "hook/hotkeys.h"
#include "hook/input.h"
#include "hook/sdl.h"
#include "overlay/overlay.h"
#include "script/script.h"

// Hotkeys from config.txt (hotkey_*; defaults: Pause / Ctrl+F12 overlay, Ctrl+F9 console, Ctrl+F10
// state dump, Ctrl+F11 snapshot, Ctrl+Shift+F11 UI capture, Ctrl+Shift+F10 UI recording). A key matches when its modifiers are
// exactly the ones named. Returns 1 if the event was consumed (the game never sees it, nor its key-up).
enum { HK_OVERLAY, HK_CONSOLE, HK_DUMP, HK_SNAPSHOT, HK_UI_CAPTURE, HK_UI_RECORD, HK_COUNT };
static struct { int sym, mods; } g_keys[HK_COUNT][4];
static int g_nkeys[HK_COUNT];

void hotkeys_load(void) {
    static const char *KEY[HK_COUNT] = { "hotkey_overlay", "hotkey_console", "hotkey_state_dump", "hotkey_snapshot", "hotkey_ui_capture", "hotkey_ui_record" };
    for (int a = 0; a < HK_COUNT; ++a) {
        const char *spec = cfg_str(KEY[a], "");
        g_nkeys[a] = 0;
        while (*spec && g_nkeys[a] < 4) {
            int sym, mods;
            if (key_parse(spec, &sym, &mods)) { g_keys[a][g_nkeys[a]].sym = sym; g_keys[a][g_nkeys[a]].mods = mods; ++g_nkeys[a]; }
            else hlog("config: %s: can't read key '%s'", KEY[a], spec);
            const char *c = strchr(spec, ',');
            if (!c) break;
            spec = c + 1;
        }
    }
}

static int hotkey_action(int32_t sym, int32_t mod) {
    int m = (mod & KMOD_CTRL ? HK_CTRL : 0) | (mod & KMOD_SHIFT ? HK_SHIFT : 0) | (mod & KMOD_ALT ? HK_ALT : 0);
    for (int a = 0; a < HK_COUNT; ++a)
        for (int k = 0; k < g_nkeys[a]; ++k)
            if (g_keys[a][k].sym == sym && g_keys[a][k].mods == m) return a;
    return -1;
}

int hotkeys_handle(const uint8_t *e) {
    static int32_t held = -1;
    int32_t sym = *(const int32_t *)(e + 8);
    int32_t mod = *(const int32_t *)(e + 12);
    if (e[0] == SDL_KEYUP) {
        if (sym == held) { held = -1; return 1; }
        return 0;
    }
    if (e[0] != SDL_KEYDOWN) return 0;
    switch (hotkey_action(sym, mod)) {
    case HK_OVERLAY:    overlay_toggle(); break;
    case HK_CONSOLE:    script_toggle_console(); break;
    case HK_DUMP:       state_dump(); break;
    case HK_SNAPSHOT:   snapshot_write(); break;
    case HK_UI_CAPTURE: ui_capture(); break;
    case HK_UI_RECORD:  record_toggle(); break;
    default: return 0;
    }
    held = sym;
    return 1;
}
