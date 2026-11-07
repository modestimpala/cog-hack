// SPDX-License-Identifier: MPL-2.0
// cog-hack: SDL.dll proxy. The real SDL is SDL_orig.dll; every export we don't define here is
// forwarded by the generated build/sdl.def. We hook:
//   SDL_PollEvent    input: hotkeys, synthetic keys, queued script lines, plugin ticks and UI events (main thread)
//   SDL_Flip         frame: the game presents nearly every frame with SDL_Flip (FUN_00426d30/dd0)
//   SDL_UpdateRect   frame: used only around mode changes/loading (FUN_00423210)
//   SDL_SetVideoMode logged
// Both present paths go through frame() so the overlay and onFrame see every presented frame.
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "base/config.h"
#include "base/log.h"
#include "debug/crash.h"
#include "debug/record.h"
#include "game/player.h"
#include "game/version.h"
#include "hook/hotkeys.h"
#include "hook/input.h"
#include "hook/sdl.h"
#include "hook/ui_events.h"
#include "overlay/intro.h"
#include "overlay/overlay.h"
#include "overlay/title.h"
#include "script/plugin_host.h"
#include "script/script.h"

static PollEvent_t    real_PollEvent;
static UpdateRect_t   real_UpdateRect;
static Flip_t         real_Flip;
static SetVideoMode_t real_SetVideoMode;
FillRect_t real_FillRect;
MapRGB_t   real_MapRGB;

static unsigned long g_frames;

static void init(void) {
    static int done;
    if (done) return;
    done = 1;
    CreateDirectoryA("hack", NULL);
    log_open("hack\\hook.log");
    config_load("hack\\config.txt");
    crash_init();               // first: a fault anywhere after this gets a report
    version_init();             // build fingerprint + signature check (safe mode on an unknown layout)
    hotkeys_load();
    overlay_init(cfg_int("overlay", 1));
    HMODULE h = GetModuleHandleA("SDL_orig.dll");
    if (!h) h = LoadLibraryA("SDL_orig.dll");
    if (!h) { MessageBoxA(NULL, "SDL_orig.dll not found", "cog-hack", MB_ICONERROR); ExitProcess(1); }
#define R(name) real_##name = (name##_t)GetProcAddress(h, "SDL_" #name)
    R(PollEvent); R(Flip); R(UpdateRect); R(SetVideoMode); R(FillRect); R(MapRGB);
#undef R
    hlog("cog-hack loaded (dev_mode=%d)", cfg_int("dev_mode", 0));
}

__declspec(dllexport) int __cdecl SDL_PollEvent(void *ev) {
    init();
    crash_phase("script / plugin tick");
    game_tick();     // invalidate per-frame caches (player pointer)
    script_init();   // no-op after the first call
    script_pump();   // queued console lines run here, where the game handles input
    plugins_tick();  // plugin on_tick callbacks (same place)
    ui_events_tick();  // plugin hover-change events
    int r = 1;
    if (!input_pop(ev)) {
        for (;;) {
            crash_phase("SDL_PollEvent");
            r = real_PollEvent(ev);
            if (!r || !ev) break;
            crash_phase("hotkeys / plugin UI events");
            if (hotkeys_handle(ev)) continue;
            if (ui_events_mouse(ev)) continue;   // a plugin swallowed this click
            break;
        }
    }
    if (r && ev) crash_note_input(ev);
    crash_phase("game");
    return r;
}

// one presented frame: onFrame + overlay; returns 1 if the overlay drew (caller must present it all)
static int frame(SDL_Surface *s) {
    init();
    game_tick();
    ++g_frames;
    crash_phase("ui recording");
    record_frame();
    crash_phase("onFrame script");
    script_frame();
    int drew = 0;
    crash_phase("overlay draw");
    if (s && overlay_visible()) { overlay_draw(s); drew = 1; }
    crash_phase("title art");
    if (title_draw(s)) drew = 1;
    crash_phase("intro line");
    if (intro_draw(s)) drew = 1;
    crash_phase("ui recording bmp");
    record_bmp();                              // after our drawing, so a recording's .bmp frames include it
    crash_phase("SDL present");                // the caller presents next; then back to the game
    return drew;
}

__declspec(dllexport) int __cdecl SDL_Flip(SDL_Surface *s) {
    int drew = frame(s);       // Flip always presents the whole surface
    int r = real_Flip(s);
    crash_phase("overlay restore");
    if (drew) overlay_restore(s);   // our pixels leave the game's surface again (see overlay.c)
    crash_phase("game");
    return r;
}

__declspec(dllexport) void __cdecl SDL_UpdateRect(SDL_Surface *s, int32_t x, int32_t y, uint32_t w, uint32_t h) {
    int drew = frame(s);
    if (drew) { x = y = 0; w = h = 0; }       // full refresh so the overlay is never left stale
    real_UpdateRect(s, x, y, w, h);
    crash_phase("overlay restore");
    if (drew) overlay_restore(s);
    crash_phase("game");
}

__declspec(dllexport) SDL_Surface *__cdecl SDL_SetVideoMode(int w, int h, int bpp, uint32_t flags) {
    init();
    SDL_Surface *s = real_SetVideoMode(w, h, bpp, flags);
    hlog("SDL_SetVideoMode(%d, %d, %d, 0x%08x) -> %p", w, h, bpp, flags, s);
    return s;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID res) {
    (void)inst; (void)res;
    if (reason == DLL_PROCESS_DETACH) {
        hlog("unload after %lu frames", g_frames);
        log_close();
    }
    return TRUE;
}
