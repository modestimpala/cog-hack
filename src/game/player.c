// SPDX-License-Identifier: MPL-2.0
// The player: entity, items, maxima, clamped setters; game time and location.
#include <stdio.h>
#include <string.h>
#include "cogmind.h"
#include "base/mem.h"
#include "game/player.h"
#include "game/version.h"

// ---------------- per-frame cache ----------------
// The player pointer only changes across level transitions / load, but resolving it costs ~8 reads.
// Cache it until the next game_tick() (called by the hook on every poll and frame) or command.
static uint32_t g_epoch = 1, g_player_epoch, g_player;

void game_tick(void) { ++g_epoch; }

uint32_t get_player(void) {
    if (g_player_epoch == g_epoch) return g_player;
    g_player_epoch = g_epoch;
    g_player = 0;
    uint32_t world = rd32(ADDR_WORLD_PTR);
    if (!world) return 0;
    uint32_t p = resolve(ADDR_ENT_REGISTRY, rd32(world + WORLD_PLAYER_HANDLE));
    char name[16];
    if (!p || !rd_string(p + ENT_NAME, name, sizeof name) || strcmp(name, "Cogmind")) return 0;  // sanity
    return g_player = p;
}

int read_player(player_info *pi) {
    memset(pi, 0, sizeof *pi);
    uint32_t p = get_player();
    int32_t f[ENT_MATTER / 4 + 1];                 // one read covers +0 .. +0x94
    if (!p || !rd(p, f, sizeof f)) return 0;
    pi->addr = p;
    pi->core = f[ENT_CORE / 4];
    pi->energy = f[ENT_ENERGY / 4];
    pi->matter = f[ENT_MATTER / 4];
    pi->core_coverage = f[ENT_CORE_COVERAGE / 4];
    pi->x = f[ENT_POS_X / 4];
    pi->y = f[ENT_POS_Y / 4];
    pi->corruption = rdi(p + ENT_CORRUPTION);
    uint32_t v[2];
    if (rd(p + ENT_ITEMS_VEC, v, sizeof v) && v[0] && v[1] >= v[0]) { pi->items_vec = v[0]; pi->n_items = (v[1] - v[0]) / 4; }
    return 1;
}

uint32_t player_item(uint32_t i) {
    player_info pi;
    if (!read_player(&pi) || i >= pi.n_items) return 0;
    return resolve(ADDR_ITEM_REGISTRY, rd32(pi.items_vec + i * 4));
}

// ---------------- maxima (calls into game code; signatures checked once) ----------------
typedef int (__attribute__((thiscall)) *ent_int_fn)(void *self);
static const uint8_t SIG_MAX_ENERGY[] = { 0x55,0x8b,0xec,0x83,0xec,0x10,0x89,0x4d,0xf4,0x8b,0x45,0xf4,0x8b,0x48,0x08,0x83 };
static const uint8_t SIG_MAX_MATTER[] = { 0x55,0x8b,0xec,0x83,0xec,0x08,0x89,0x4d,0xfc,0x8b,0x4d,0xfc };
static int g_sig_energy, g_sig_matter;

static int call_ent_int(uint32_t fn, const uint8_t *sig, size_t n, int *cache) {
    uint32_t p = get_player();
    if (!p || !version_writes_allowed() || !sig_ok(fn, sig, n, cache)) return 0;
    return ((ent_int_fn)(uintptr_t)fn)((void *)(uintptr_t)p);
}
int player_max_core(void)   { return get_player() ? rdi(ADDR_PLAYER_MAX_CORE) : 0; }
int player_max_energy(void) { return call_ent_int(FN_ENT_MAX_ENERGY, SIG_MAX_ENERGY, sizeof SIG_MAX_ENERGY, &g_sig_energy); }
int player_max_matter(void) { return call_ent_int(FN_ENT_MAX_MATTER, SIG_MAX_MATTER, sizeof SIG_MAX_MATTER, &g_sig_matter); }

// ---------------- clamped setters ----------------
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static int set_pfield(int off, int v) { uint32_t p = get_player(); return p && wr32(p + off, (uint32_t)v); }
int set_player_core(int v)       { int m = player_max_core();   return m > 0 && set_pfield(ENT_CORE, clampi(v, 1, m)); }
int set_player_energy(int v)     { int m = player_max_energy(); return set_pfield(ENT_ENERGY, clampi(v, 0, m)); }
int set_player_matter(int v)     { int m = player_max_matter(); return set_pfield(ENT_MATTER, clampi(v, 0, m)); }
int set_player_corruption(int v) { return set_pfield(ENT_CORRUPTION, clampi(v, 0, 100)); }
int set_player_item_hp(uint32_t i, int v) {
    uint32_t it = player_item(i);
    if (!it) return 0;
    int m = rdi(rd32(it + ITEM_DEF_PTR) + IDEF_INTEGRITY);
    return m > 0 && wr32(it + ITEM_INTEGRITY, (uint32_t)clampi(v, 1, m));
}

int player_heat(void) { uint32_t p = get_player(); return p ? rdi(p + ENT_HEAT) : 0; }
int game_time(void)   { uint32_t w = rd32(ADDR_WORLD_PTR); return w ? rdi(w + WORLD_GAME_TIME) : 0; }

int location_text(char *out, size_t cap) {
    uint32_t loc = resolve(ADDR_LOC_REGISTRY, rd32(ADDR_CUR_LOCATION));
    int32_t f[2];
    uint8_t shown = 0;
    char name[32] = "???";
    if (!cap) return 0;
    out[0] = 0;
    if (!loc || !rd(loc + LOC_MAP_TYPE, f, 8)) return 0;
    rd(loc + LOC_NAME_SHOWN, &shown, 1);
    if (shown && (f[0] < 0 || f[0] >= MAP_NAME_COUNT || !rd_string(ADDR_MAP_NAMES + 28 * (uint32_t)f[0], name, sizeof name)))
        snprintf(name, sizeof name, "?");
    snprintf(out, cap, "-%d/%s", f[1], name);
    return 1;
}
