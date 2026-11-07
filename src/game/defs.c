// SPDX-License-Identifier: MPL-2.0
// Item and robot definition tables (name lookups cached) and their named numeric fields.
#include <stdlib.h>
#include <string.h>
#include "cogmind.h"
#include "base/mem.h"
#include "game/defs.h"

// ---------------- definition tables ----------------
// std::vector<T*> size, 0 unless it looks like one: aligned, begin <= end <= capacity, at most
// 64K entries (the game's tables are ~1.4K). Guards against reading garbage as a table (in the
// test process, say, or after a game update moves things) and looping over billions of "entries".
static uint32_t vec_count(uint32_t vec) {
    uint32_t v[3];
    if (!rd(vec, v, sizeof v) || !v[0] || (v[0] & 3) || v[1] < v[0] || v[2] < v[1] || (v[1] - v[0]) & 3) return 0;
    uint32_t n = (v[1] - v[0]) / 4;
    return n <= 0x10000 ? n : 0;
}
static uint32_t vec_at(uint32_t vec, uint32_t i) { return i < vec_count(vec) ? rd32(rd32(vec) + i * 4) : 0; }

uint32_t item_def_count(void)   { return vec_count(ADDR_ITEM_DEFS); }
uint32_t item_def(uint32_t idx) { return vec_at(ADDR_ITEM_DEFS, idx); }
uint32_t bot_def_count(void)    { return vec_count(ADDR_BOT_DEFS); }
uint32_t bot_def(uint32_t idx)  { return vec_at(ADDR_BOT_DEFS, idx); }

// Name -> index caches. Def tables are built once at startup; rebuild if the table ever changes
// (count or backing storage pointer).
typedef struct { int vec_id; int name_off; uint32_t n, storage; char (*names)[64]; uint32_t *ptrs; } def_cache;
static def_cache g_items = { CM_A_ADDR_ITEM_DEFS, IDEF_NAME, 0, 0, 0, 0 };   // address table slots: see cogmind.h
static def_cache g_bots  = { CM_A_ADDR_BOT_DEFS,  BDEF_NAME, 0, 0, 0, 0 };

static int cache_fresh(def_cache *c) {
    uint32_t n = vec_count(cm_addr_table[c->vec_id]), storage = rd32(cm_addr_table[c->vec_id]);
    if (!n) return 0;
    if (c->names && c->n == n && c->storage == storage) return 1;
    free(c->names); free(c->ptrs);
    c->names = calloc(n, sizeof *c->names);
    c->ptrs = calloc(n, sizeof *c->ptrs);
    if (!c->names || !c->ptrs) { free(c->names); free(c->ptrs); c->names = 0; c->ptrs = 0; c->n = 0; return 0; }
    if (!rd(storage, c->ptrs, n * 4)) { c->n = 0; return 0; }
    for (uint32_t i = 0; i < n; ++i) rd_string(c->ptrs[i] + c->name_off, c->names[i], sizeof c->names[i]);
    c->n = n; c->storage = storage;
    return 1;
}
static int cache_find(def_cache *c, const char *name) {
    if (!cache_fresh(c)) return -1;
    for (uint32_t i = 0; i < c->n; ++i) if (!strcmp(c->names[i], name)) return (int)i;
    return -1;
}
int item_def_find(const char *name) { return cache_find(&g_items, name); }
int bot_def_find(const char *name)  { return cache_find(&g_bots, name); }
int item_def_index(uint32_t d) {
    if (!d || !cache_fresh(&g_items)) return -1;
    for (uint32_t i = 0; i < g_items.n; ++i) if (g_items.ptrs[i] == d) return (int)i;
    return -1;
}

// ---------------- named numeric fields ----------------
typedef struct { const char *name; int off; } field;
static const field ITEM_FIELDS[] = {
    {"size",IDEF_SIZE},{"rating",IDEF_RATING},{"mass",IDEF_MASS},{"integrity",IDEF_INTEGRITY},
    {"coverage",IDEF_COVERAGE},{"heat",IDEF_HEAT_GEN},{"energy_gen",IDEF_ENERGY_GEN},
    {"energy_store",IDEF_ENERGY_STORE},{"time_per_move",IDEF_TIME_PER_MOVE},{"drag",IDEF_DRAG},
    {"support",IDEF_SUPPORT},{"range",IDEF_RANGE},{"shot_energy",IDEF_SHOT_ENERGY},
    {"shot_matter",IDEF_SHOT_MATTER},{"shot_heat",IDEF_SHOT_HEAT},{"delay",IDEF_DELAY},
    {"projectiles",IDEF_PROJECTILES},{"damage_min",IDEF_DAMAGE_MIN},{"damage_max",IDEF_DAMAGE_MAX},
    {"targeting",IDEF_TARGETING},{"recoil",IDEF_RECOIL},{0,0}};
static const field BOT_FIELDS[] = {
    {"tier",BDEF_TIER},{"threat",BDEF_THREAT},{"value",BDEF_VALUE},{"memory",BDEF_MEMORY},
    {"spot",BDEF_SPOT},{"size",BDEF_SIZE},{"inventory",BDEF_INV_CAPACITY},{"core",BDEF_CORE},
    {"energy_store",BDEF_ENERGY_STORE},{"energy_gen",BDEF_ENERGY_GEN},{"matter_store",BDEF_MATTER_STORE},
    {"heat_dissipation",BDEF_HEAT_DISSIP},{"sight",BDEF_SIGHT},{0,0}};

static int field_off(const field *t, const char *n) {
    for (; t->name; ++t) if (!strcmp(t->name, n)) return t->off;
    return -1;
}
static const char *field_list(const field *t, char *buf, size_t cap) {
    if (buf[0]) return buf;                          // built once
    for (; t->name; ++t) {
        strncat(buf, t->name, cap - strlen(buf) - 1);
        if (t[1].name) strncat(buf, ", ", cap - strlen(buf) - 1);
    }
    return buf;
}
int item_field_offset(const char *f) { return field_off(ITEM_FIELDS, f); }
int bot_field_offset(const char *f)  { return field_off(BOT_FIELDS, f); }
const char *item_field_names(void) { static char b[512]; return field_list(ITEM_FIELDS, b, sizeof b); }
const char *bot_field_names(void)  { static char b[512]; return field_list(BOT_FIELDS, b, sizeof b); }
