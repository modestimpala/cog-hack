// SPDX-License-Identifier: MPL-2.0
// The player, game time, location. Cached until the next game_tick().
#pragma once
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint32_t addr;
    int core, energy, matter, corruption, core_coverage, x, y;
    uint32_t n_items, items_vec;
} player_info;

void     game_tick(void);           // poll/frame boundary: drops per-frame caches
uint32_t get_player(void);          // entity address or 0
int      read_player(player_info *pi);
uint32_t player_item(uint32_t i);   // item address or 0
int      player_max_core(void);
int      player_max_energy(void);   // calls game code
int      player_max_matter(void);   // calls game code
// clamped setters: core 1..max, energy/matter 0..max, corruption 0..100, item hp 1..max
int      set_player_core(int v);
int      set_player_energy(int v);
int      set_player_matter(int v);
int      set_player_corruption(int v);
int      set_player_item_hp(uint32_t i, int v);
int      player_heat(void);
int      game_time(void);                           // HUD "Time:"
int      location_text(char *out, size_t cap);      // "-7/Factory" ("???" when the HUD hides it); 1 ok
#ifdef __cplusplus
}
#endif
