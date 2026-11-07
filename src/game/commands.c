// SPDX-License-Identifier: MPL-2.0
// Game command dispatch: the same path the key-binding code takes (FUN_00427460):
//   sink = XRoot (state manager +0x6c); Command c(id[, x, y]); sink->vtable[4](&c)
#include <windows.h>
#include <string.h>
#include "cogmind.h"
#include "base/log.h"
#include "base/mem.h"
#include "game/commands.h"
#include "game/player.h"
#include "game/version.h"

// The 423 command names are static std::strings initialised before any of our code runs; cache them.
static char g_names[COMMAND_COUNT][48];
static int g_names_ok;

static int names_ready(void) {
    if (g_names_ok) return 1;
    for (int i = 0; i < COMMAND_COUNT; ++i)
        if (!rd_string(ADDR_COMMAND_NAMES + i * MSVC_STRING_SIZE, g_names[i], sizeof g_names[i]) || !g_names[i][0])
            return 0;                                // not readable (wrong build / test exe): retry next time
    return g_names_ok = 1;
}

int command_name(int id, char *out, size_t cap) {
    out[0] = 0;
    if (id < 0 || id >= COMMAND_COUNT || !names_ready()) return 0;
    strncpy(out, g_names[id], cap - 1);
    out[cap - 1] = 0;
    return 1;
}

int command_find(const char *name) {
    if (!names_ready()) return -1;
    for (int i = 0; i < COMMAND_COUNT; ++i) if (!strcmp(g_names[i], name)) return i;
    return -1;
}

typedef void (__attribute__((thiscall)) *cmd_ctor_t)(void *self, int id);
typedef void (__attribute__((thiscall)) *cmd_ctor_pos_t)(void *self, int id, int x, int y);
typedef int  (__attribute__((thiscall)) *cmd_handle_t)(void *self, void *cmd);

static const uint8_t SIG_CTOR[] = { 0x55,0x8b,0xec,0x51,0x89,0x4d,0xfc,0x8b,0x45,0xfc,0x8b,0x4d,0x08,0x89,0x08,0x8b };
static int g_sig_ctor, g_sig_ctor_pos;

static int dispatch(int id, int has_pos, int x, int y) {
    if (id < 0 || id >= COMMAND_COUNT || !version_writes_allowed() || !names_ready()) return -1;
    if (strstr(g_names[id], "DELETE_QUIT")) {        // would delete the current run's save
        hlog("command %d %s refused (destructive)", id, g_names[id]);
        return -1;
    }
    if (has_pos ? !sig_ok(FN_COMMAND_CTOR_POS, SIG_CTOR, sizeof SIG_CTOR, &g_sig_ctor_pos)
                : !sig_ok(FN_COMMAND_CTOR, SIG_CTOR, sizeof SIG_CTOR, &g_sig_ctor)) return -1;
    uint32_t sink = rd32(ADDR_STATE_MGR + STATE_MGR_ROOT);
    uint32_t fn = sink ? rd32(rd32(sink) + XROOT_VT_HANDLE) : 0;
    if (fn != FN_XROOT_HANDLE) {                     // no sink yet, or layout changed: don't call the unknown
        if (fn) hlog("command %d: sink handler %08x != expected %08x, refusing", id, fn, FN_XROOT_HANDLE);
        return -1;
    }
    uint8_t cmd[64] __attribute__((aligned(16)));    // Command is ~12 bytes; pad generously
    memset(cmd, 0, sizeof cmd);
    if (has_pos) ((cmd_ctor_pos_t)FN_COMMAND_CTOR_POS)(cmd, id, x, y);
    else ((cmd_ctor_t)FN_COMMAND_CTOR)(cmd, id);
    int r = ((cmd_handle_t)FN_XROOT_HANDLE)((void *)(uintptr_t)sink, cmd) & 0xff;
    game_tick();                                     // a command can change game state: drop caches
    return r;
}

int game_command(int id)                  { return dispatch(id, 0, 0, 0); }
int game_command_at(int id, int x, int y) { return dispatch(id, 1, x, y); }

// The game calls *(ADDR_CMD_OBSERVER)(Command*) before dispatching every command, if set (it's null).
int install_command_observer(void (*fn)(void *cmd)) {
    //   0042da99: 83 3d 0c 36 d2 00 00   cmp dword [0xd2360c], 0  : the code that reads the slot
    static const uint8_t SIG[] = { 0x83, 0x3d, 0x0c, 0x36, 0xd2, 0x00, 0x00 };
    static int sig;
    if (!sig_ok(0x0042da99, SIG, sizeof SIG, &sig)) { hlog("command events disabled"); return 0; }
    uint32_t cur = rd32(ADDR_CMD_OBSERVER);
    if (cur && cur != (uint32_t)(uintptr_t)fn) {
        hlog("command observer slot already used by %08x; not installing", cur);
        return 0;
    }
    if (!wr32(ADDR_CMD_OBSERVER, (uint32_t)(uintptr_t)fn)) return 0;   // .data: writable
    hlog("command observer installed");
    return 1;
}
