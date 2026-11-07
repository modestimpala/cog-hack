// SPDX-License-Identifier: MPL-2.0
// UI recordings for reverse engineering animations: while a console of class `ui_record` (config.txt)
// is visible, the cells of its children are written every frame they change, to
// hack\snapshots\rec_<tick>.bin. Stops after ui_record_max_s seconds, when the console goes away,
// or with the hotkey. Format (little-endian), read by tools/re/uirec.py:
//   "CMREC1\0\0", then per frame: "FRAM" u32 tick u32 ncons, per console:
//   u32 addr, char cls[32], i32 w, i32 h, i32 ax, i32 ay, u8 cells[w*h*20] (the game's cell layout)
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cogmind.h"
#include "cm_ui.h"
#include "base/config.h"
#include "base/log.h"
#include "base/mem.h"
#include "debug/record.h"
#include "debug/snapshot.h"

#define MAX_FRAMES 1500
#define MAX_RECORDINGS 3

static FILE *g_f;
static DWORD g_t0;
static char g_path[MAX_PATH];
static int g_frames, g_gone, g_recordings, g_manual;
static uint8_t *g_last, *g_cur;     // the previous frame's blocks, to skip unchanged frames
static size_t g_last_len, g_cur_len, g_cap;

static void stop(const char *why) {
    if (!g_f) return;
    fclose(g_f);
    g_f = NULL;
    hlog("ui recording stopped (%s): %d frames, %lu ms", why, g_frames, GetTickCount() - g_t0);
    g_last_len = 0;
}

static void start(void) {
    char *path = g_path;
    CreateDirectoryA("hack\\snapshots", NULL);
    snprintf(path, MAX_PATH, "hack\\snapshots\\rec_%lu.bin", GetTickCount());
    g_f = fopen(path, "wb");
    if (!g_f) return;
    fwrite("CMREC1\0\0", 1, 8, g_f);
    g_t0 = GetTickCount();
    g_frames = g_gone = 0;
    ++g_recordings;
    hlog("ui recording -> %s (console %s)", path, cfg_str("ui_record", ""));
}

static int append(const void *p, size_t n) {
    if (g_cur_len + n > g_cap) {
        size_t cap = (g_cur_len + n) * 2;
        uint8_t *nb = realloc(g_cur, cap);
        if (!nb) return 0;
        g_cur = nb; g_cap = cap;
    }
    memcpy(g_cur + g_cur_len, p, n);
    g_cur_len += n;
    return 1;
}

// the console's own cells if it is small, then its visible descendants
static int collect(uint32_t a, int depth, int self) {
    cm_ui_node n;
    if (depth > 12 || !cm_ui_node_read(rd, a, &n) || n.hidden) return 0;
    int count = 0;
    size_t bytes = (size_t)n.w * n.h * CELL_SIZE;
    if (n.cells && n.w && n.h && (!self || bytes <= 65536)) {
        uint8_t *cells = malloc(bytes);
        if (cells && rd(n.cells, cells, bytes)) {
            struct { uint32_t addr; char cls[32]; int32_t w, h, ax, ay; } h = { n.addr, {0}, n.w, n.h, n.ax, n.ay };
            snprintf(h.cls, sizeof h.cls, "%.31s", n.cls);
            if (append(&h, sizeof h) && append(cells, bytes)) ++count;
        }
        free(cells);
    }
    for (uint32_t i = 0; i < n.nkids; ++i) count += collect(cm_ui_child(rd, &n, i), depth + 1, 0);
    return count;
}

void record_toggle(void) {
    if (g_f) { g_manual = 0; stop("hotkey"); return; }
    if (!*cfg_str("ui_record", "")) { hlog("ui recording: set ui_record=<console class> in config.txt first"); return; }
    g_manual = 1;
    start();
}

void record_frame(void) {
    const char *cls = cfg_str("ui_record", "");
    if (!*cls) return;
    uint32_t root = cm_ui_root(rd);
    uint32_t target = root ? cm_ui_find(rd, root, cls, 1) : 0;
    if (!g_f) {
        if (!target || g_manual || g_recordings >= MAX_RECORDINGS) return;
        start();                                   // the console just appeared: record from its first frame
        if (!g_f) return;
    }
    if (!target) {
        if (++g_gone > 60) { g_manual = 0; stop("console gone"); }
        return;
    }
    g_gone = 0;
    g_cur_len = 0;
    int n = collect(target, 0, 1);
    if (n && g_cur_len == g_last_len && !memcmp(g_cur, g_last, g_cur_len)) return;   // nothing changed
    struct { char tag[4]; uint32_t tick, ncons; } fh = { {'F', 'R', 'A', 'M'}, GetTickCount(), (uint32_t)n };
    fwrite(&fh, 1, sizeof fh, g_f);
    fwrite(g_cur, 1, g_cur_len, g_f);
    ++g_frames;
    if (g_cur_len > g_last_len || !g_last) { uint8_t *nb = realloc(g_last, g_cur_len); if (nb) g_last = nb; else return; }
    memcpy(g_last, g_cur, g_cur_len);
    g_last_len = g_cur_len;
    if (g_frames >= MAX_FRAMES) { g_manual = 0; stop("frame limit"); }
    else if (GetTickCount() - g_t0 > (DWORD)cfg_int("ui_record_max_s", 30) * 1000) { g_manual = 0; stop("time limit"); }
}

void record_bmp(void) {
    static int presented;
    int every = cfg_int("ui_record_bmp", 0);
    if (!g_f || every <= 0 || ++presented % every) return;
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%.*s_%04d.bmp", (int)strlen(g_path) - 4, g_path, presented);
    screen_bmp(path);
}
