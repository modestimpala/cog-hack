// SPDX-License-Identifier: MPL-2.0
// Ctrl+F11 memory snapshots (minidumps for tools/re/minidump.py), Ctrl+Shift+F11 UI captures and
// Ctrl+F10 text state dumps.
//
// PRIVACY: a snapshot is a copy of the game's memory. It can contain anything the process holds
// (Steam session data, file paths, your user name). Don't post snapshots publicly.
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "cogmind.h"
#include "cm_ui.h"
#include "base/config.h"
#include "base/log.h"
#include "base/mem.h"
#include "debug/minidump.h"
#include "debug/snapshot.h"
#include "game/player.h"
#include "hook/sdl.h"

#define SNAP_DIR "hack\\snapshots"

// ---- sidecars: what was on screen, so a snapshot needs no separate screenshot ----
static void tree(FILE *f, uint32_t a, int depth) {
    cm_ui_node n;
    if (depth > 20 || !cm_ui_node_read(rd, a, &n)) return;
    char t[256];
    cm_ui_row_text(rd, &n, 0, t, sizeof t);
    fprintf(f, "%*s%s%s %d,%d %dx%d font %d  %08x  %s\n", depth * 2, "", n.hidden ? "(hidden) " : "", n.cls,
            n.ax, n.ay, n.rw, n.rh, n.font, n.addr, t);
    if (n.hidden) return;                                   // children of hidden windows: skipped
    for (uint32_t i = 0; i < n.nkids; ++i) tree(f, cm_ui_child(rd, &n, i), depth + 1);
}

static void write_text_sidecar(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    static char buf[128 * 1024];
    cm_mouse m;
    if (cm_ui_mouse(rd, &m)) {
        cm_ui_describe(rd, m.hovered, buf, sizeof buf);
        fprintf(f, "mouse px %d,%d cell %d,%d\nhovered (game): %s\n", m.px, m.py, m.cx, m.cy, buf);
        cm_ui_describe(rd, cm_ui_at(rd, m.cx, m.cy), buf, sizeof buf);
        fprintf(f, "under cursor:   %s\n", buf);
        int wx, wy;
        if (cm_ui_map_tile(rd, m.cx, m.cy, &wx, &wy)) fprintf(f, "map tile under cursor: %d,%d\n", wx, wy);
    }
    char loc[48] = "";
    location_text(loc, sizeof loc);
    fprintf(f, "location %s  time %d\n\n", loc, game_time());
    cm_screen_text(rd, 0, 0, 0, 0, buf, sizeof buf);
    fprintf(f, "---- screen (root cells; tiles shown as \xc2\xb7) ----\n%s\n\n---- ui tree ----\n", buf);
    tree(f, cm_ui_root(rd), 0);
    fclose(f);
}

typedef void *(__cdecl *RWFromFile_t)(const char *file, const char *mode);
typedef int   (__cdecl *SaveBMP_RW_t)(SDL_Surface *s, void *dst, int freedst);

int screen_bmp(const char *path) {
    static RWFromFile_t rw;
    static SaveBMP_RW_t save;
    if (!rw) {
        HMODULE h = GetModuleHandleA("SDL_orig.dll");
        if (h) { rw = (RWFromFile_t)GetProcAddress(h, "SDL_RWFromFile"); save = (SaveBMP_RW_t)GetProcAddress(h, "SDL_SaveBMP_RW"); }
        if (!rw || !save) { rw = NULL; return 0; }
    }
    SDL_Surface *s = (SDL_Surface *)(uintptr_t)rd32(ADDR_SCREEN_SURFACE);
    void *dst = s ? rw(path, "wb") : NULL;
    if (!dst) return 0;
    return save(s, dst, 1) == 0;
}

void snapshot_write(void) {
    CreateDirectoryA(SNAP_DIR, NULL);
    char path[MAX_PATH];
    snprintf(path, sizeof path, SNAP_DIR "\\snap_%lu.dmp", GetTickCount());
    DWORD t0 = GetTickCount();
    int ok = minidump_write(path, cfg_int("snapshot_full", 0), NULL, 0);
    WIN32_FILE_ATTRIBUTE_DATA fa;
    double mb = GetFileAttributesExA(path, GetFileExInfoStandard, &fa) ? (fa.nFileSizeLow + 4294967296.0 * fa.nFileSizeHigh) / 1048576.0 : 0;
    hlog("snapshot %s -> %s (%lu ms, %.1f MB): contains process memory, don't share publicly",
         path, ok ? "ok" : "FAILED", GetTickCount() - t0, mb);
    size_t len = strlen(path);
    strcpy(path + len - 4, ".txt");
    write_text_sidecar(path);
    if (cfg_int("snapshot_bmp", 1)) { strcpy(path + len - 4, ".bmp"); screen_bmp(path); }
    minidump_prune(SNAP_DIR, "snap_*.dmp", cfg_int("snapshot_keep", 3), ".bmp");   // the .txt files are kept
}

// Ctrl+Shift+F11: just the UI text capture (screen text, hover, UI tree): no memory dump, never pruned
void ui_capture(void) {
    char path[MAX_PATH];
    CreateDirectoryA(SNAP_DIR, NULL);
    snprintf(path, sizeof path, SNAP_DIR "\\ui_%lu.txt", GetTickCount());
    write_text_sidecar(path);
    hlog("ui capture -> %s", path);
}

void state_dump(void) {
    char path[MAX_PATH];
    CreateDirectoryA(SNAP_DIR, NULL);
    snprintf(path, sizeof path, SNAP_DIR "\\state_%lu.txt", GetTickCount());
    dump_state(path);
    hlog("state dump -> %s", path);
}
