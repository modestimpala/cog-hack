/* SPDX-License-Identifier: MIT */
/* cm_state.h: plugin-side helpers (header-only). Read the player's state through the host API into
 * plain structs, plus build math and combat-log parsing over those structs.
 *
 *   cm_state st;
 *   cm_math m;
 *   if (cm_read_state(api, &st)) cm_compute(&st, &m);
 *
 * Read-only: only api->read_mem / read_string / player / resolve_item / max_* are used.
 * Call on the game thread (from an on_tick callback or a script function).
 * The functions that take a cm_state but no api are pure, so they can be unit-tested offline.
 */
#ifndef CM_STATE_H
#define CM_STATE_H
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "plugin_api.h"
#include "cogmind.h"
#include "cm_addr.h"

#define CM_MAX_PARTS 64

typedef struct {
    uint32_t addr, def;
    char  name[48];
    int   loc;                 /* enum item_loc: 0-3 attached slot kind, 4 inventory */
    int   active;              /* attached and switched on (item +0x28 != -1) */
    int   slot, type, size;    /* from the def: slot kind it fits, enum item_type, slots it takes */
    int   hp, max_hp, mass, coverage, rating;
    int   energy_gen, energy_store, heat_gen;
    float upkeep, energy_per_move;
    int   support, penalty, time_per_move, heat_per_move;
    int   shot_energy, shot_matter, shot_heat, dmg_min, dmg_max, projectiles, range, delay;
} cm_part;

typedef struct {
    int in_game;
    uint32_t addr;
    int core, max_core, energy, max_energy, matter, max_matter, corruption, x, y;
    int slots[4];              /* total slots per kind: power, propulsion, utility, weapon */
    int innate_energy_gen;     /* Cogmind's own core output (BotDef 0) */
    int n;
    cm_part p[CM_MAX_PARTS];   /* equipped first, then inventory (game order) */
    int time;                  /* game time (HUD "Time:"), world +0x314 */
    int heat;                  /* current heat (HUD "Heat"), entity +0x98 */
    int depth;                 /* positive: 7 = -7 (0 = unknown) */
    int map_type;              /* index into the map-name table, -1 unknown */
    char map_name[32];         /* as the HUD shows it: "Factory", or "???" when the game hides it */
    int evolutions;            /* (max core - base core) / 150 */
    int base_heat_dissipation; /* innate 25 + 3 per evolution (cooling parts added in cm_compute) */
    int now_turn;              /* max(time, newest main-log message turn) */
    int event_turn[8];         /* per CM_EV_*: turn of the newest matching log message, 0 = none */
} cm_state;

/* ---------------- message log ---------------- */
typedef struct { int seq, turn; char text[160]; } cm_msg;

/* Read the last `max` messages of a log (ADDR_MSG_LOG or ADDR_COMBAT_LOG) into out, oldest first.
 * Returns how many were read. */
static inline int cm_read_log(const cm_host_api *api, uint32_t log, int max, cm_msg *out) {
    uint32_t v[2];
    if (max <= 0 || !api->read_mem(log, v, 8) || v[1] <= v[0] || (v[1] - v[0]) % 4) return 0;
    int total = (int)((v[1] - v[0]) / 4), first = total > max ? total - max : 0, n = 0;
    for (int i = first; i < total; ++i) {
        uint32_t m;
        int32_t st[2];
        if (!api->read_mem(v[0] + 4 * (uint32_t)i, &m, 4) || !m || !api->read_mem(m + LOGMSG_SEQ, st, 8)) continue;
        if (!api->read_string(m + LOGMSG_TEXT, out[n].text, sizeof out[n].text)) continue;
        out[n].seq = st[0];
        out[n].turn = st[1];
        ++n;
    }
    return n;
}

/* Run-ending log events, matched against the game's own message templates. */
enum { CM_EV_OVERHEAT, CM_EV_GARRISON, CM_EV_NO_ENERGY, CM_EV_NO_MATTER, CM_EV_FAULTY, CM_EV_OVERLOAD_HEAT, CM_EV_COUNT };
static const struct { const char *needle; } CM_EVENTS[CM_EV_COUNT] = {
    { "Overheating:" },                    /* "Overheating: Core damaged (-%1)." / "%1 damaged" / bleed / decay */
    { "out of Garrison Access" },          /* "%1 tumbles out of Garrison Access." */
    { "Insufficient energy" },
    { "Insufficient matter" },
    { "Faulty integration" },              /* unidentified prototype misbehaving */
    { "Overload: Heat surge" },
};
#define CM_EVENT_WINDOW 30                 /* turns an event stays on the panel */

static inline int cm_event_of(const char *text) {
    for (int e = 0; e < CM_EV_COUNT; ++e) if (strstr(text, CM_EVENTS[e].needle)) return e;
    return -1;
}

/* fill now_turn / event_turn[] from the last 64 main-log messages */
static inline void cm_read_events(const cm_host_api *api, cm_state *s) {
    static cm_msg m[64];
    int n = cm_read_log(api, ADDR_MSG_LOG, 64, m);
    for (int i = 0; i < n; ++i) {
        if (m[i].turn > s->now_turn) s->now_turn = m[i].turn;
        int e = cm_event_of(m[i].text);
        if (e >= 0 && m[i].turn > s->event_turn[e]) s->event_turn[e] = m[i].turn;
    }
}
static inline int cm_event_recent(const cm_state *s, int e) {
    return s->event_turn[e] > 0 && s->now_turn - s->event_turn[e] <= CM_EVENT_WINDOW;
}

static const char *const CM_SLOT_NAMES[4] = { "power", "propulsion", "utility", "weapon" };
static const char *const CM_TYPE_NAMES[IT_COUNT] = {
    "Matter", "Data Core", "Scrap", "Protomatter", "Item", "Trap", "Engine", "Power Core", "Reactor",
    "Treads", "Leg", "Wheel", "Hover Unit", "Flight Unit", "Device", "Storage", "Processor", "Hackware",
    "Protection", "Artifact", "Energy Gun", "Energy Cannon", "Ballistic Gun", "Ballistic Cannon",
    "Launcher", "Special Weapon", "Impact Weapon", "Slashing Weapon", "Piercing Weapon", "Special Melee Weapon",
};
static inline const char *cm_type_name(int t) { return t >= 0 && t < IT_COUNT ? CM_TYPE_NAMES[t] : "?"; }
static inline const char *cm_slot_name(int s) { return s >= 0 && s < 4 ? CM_SLOT_NAMES[s] : "none"; }

static inline int32_t cm_i32(const uint8_t *b, int off) { int32_t v; memcpy(&v, b + off, 4); return v; }
static inline float cm_f32(const uint8_t *b, int off) { float v; memcpy(&v, b + off, 4); return v; }

/* Fill the def-derived fields of *p from an ItemDef* (name included). 1 ok. */
static inline int cm_read_def(const cm_host_api *api, uint32_t def, cm_part *p) {
    uint8_t d[0x150];
    if (!def || !api->read_mem(def, d, sizeof d)) return 0;
    p->def = def;
    if (!api->read_string(def + IDEF_NAME, p->name, sizeof p->name)) p->name[0] = 0;
    p->type = cm_i32(d, IDEF_TYPE);           p->slot = cm_i32(d, IDEF_SLOT);
    p->size = cm_i32(d, IDEF_SIZE);           p->rating = cm_i32(d, IDEF_RATING);
    p->mass = cm_i32(d, IDEF_MASS);           p->max_hp = cm_i32(d, IDEF_INTEGRITY);
    p->coverage = cm_i32(d, IDEF_COVERAGE);   p->heat_gen = cm_i32(d, IDEF_HEAT_GEN);
    p->upkeep = cm_f32(d, IDEF_ENERGY_UPKEEP);
    p->energy_gen = cm_i32(d, IDEF_ENERGY_GEN); p->energy_store = cm_i32(d, IDEF_ENERGY_STORE);
    p->time_per_move = cm_i32(d, IDEF_TIME_PER_MOVE);
    p->energy_per_move = cm_f32(d, IDEF_ENERGY_PER_MOVE);
    p->heat_per_move = cm_i32(d, IDEF_HEAT_PER_MOVE);
    p->support = cm_i32(d, IDEF_SUPPORT);     p->penalty = cm_i32(d, IDEF_PENALTY);
    p->range = cm_i32(d, IDEF_RANGE);         p->delay = cm_i32(d, IDEF_DELAY);
    p->shot_energy = cm_i32(d, IDEF_SHOT_ENERGY); p->shot_matter = cm_i32(d, IDEF_SHOT_MATTER);
    p->shot_heat = cm_i32(d, IDEF_SHOT_HEAT); p->projectiles = cm_i32(d, IDEF_PROJECTILES);
    p->dmg_min = cm_i32(d, IDEF_DAMAGE_MIN);  p->dmg_max = cm_i32(d, IDEF_DAMAGE_MAX);
    if (p->slot < 0 || p->slot > 3) p->slot = -1;
    if (p->size < 1) p->size = 1;
    return 1;
}

/* Current location as the HUD shows it. 1 ok. */
static inline int cm_read_location(const cm_host_api *api, int *depth, int *map_type, char *name, size_t cap) {
    uint32_t h = 0, reg = 0, v[2], gen = 0, loc = 0;
    int32_t f[2];
    uint8_t shown = 0;
    *depth = 0; *map_type = -1; snprintf(name, cap, "?");
    if (!api->read_mem(ADDR_CUR_LOCATION, &h, 4) || !h || !api->read_mem(ADDR_LOC_REGISTRY, &reg, 4) || !reg) return 0;
    uint32_t idx = h & 0xffff, gens = 0;
    if (!api->read_mem(reg, v, 8) || v[1] <= v[0] || idx >= (v[1] - v[0]) / 4) return 0;
    if (!api->read_mem(reg + 0x10, &gens, 4) || !api->read_mem(gens + 4 * idx, &gen, 4) || gen != h >> 16) return 0;
    if (!api->read_mem(v[0] + 4 * idx, &loc, 4) || !loc || !api->read_mem(loc + LOC_MAP_TYPE, f, 8)) return 0;
    api->read_mem(loc + LOC_NAME_SHOWN, &shown, 1);
    *map_type = f[0]; *depth = f[1];
    if (!shown) snprintf(name, cap, "???");
    else if (f[0] < 0 || f[0] >= MAP_NAME_COUNT || !api->read_string(ADDR_MAP_NAMES + 28 * (uint32_t)f[0], name, cap)) snprintf(name, cap, "?");
    return 1;
}

/* 1 if a run is in progress and *s was filled; 0 otherwise (s->in_game = 0). */
static inline int cm_read_state(const cm_host_api *api, cm_state *s) {
    uint8_t e[0x140];
    memset(s, 0, sizeof *s);
    s->addr = api->player();
    if (!s->addr || !api->read_mem(s->addr, e, sizeof e)) return 0;
    s->in_game = 1;
    s->core = cm_i32(e, ENT_CORE);     s->energy = cm_i32(e, ENT_ENERGY);
    s->matter = cm_i32(e, ENT_MATTER); s->corruption = cm_i32(e, ENT_CORRUPTION);
    s->x = cm_i32(e, ENT_POS_X);       s->y = cm_i32(e, ENT_POS_Y);
    for (int k = 0; k < 4; ++k) s->slots[k] = cm_i32(e, ENT_SLOTS_POWER + 4 * k);
    s->heat = cm_i32(e, ENT_HEAT);
    s->max_core = api->max_core();
    s->max_energy = api->max_energy(); s->max_matter = api->max_matter();
    uint32_t bd = api->bot_def(0);
    int32_t gen = 0, base_core = 0, dis = 0;
    if (bd && api->read_mem(bd + BDEF_ENERGY_GEN, &gen, 4)) s->innate_energy_gen = gen;
    if (bd && api->read_mem(bd + BDEF_CORE, &base_core, 4) && base_core > 0 && s->max_core > base_core)
        s->evolutions = (s->max_core - base_core) / 150;
    if (bd && api->read_mem(bd + BDEF_HEAT_DISSIP, &dis, 4)) s->base_heat_dissipation = dis + 3 * s->evolutions;

    uint32_t begin = (uint32_t)cm_i32(e, ENT_ITEMS_VEC), end = (uint32_t)cm_i32(e, ENT_ITEMS_VEC + 4);
    uint32_t n = end > begin ? (end - begin) / 4 : 0;
    if (n > CM_MAX_PARTS) n = CM_MAX_PARTS;
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t h, it;
        uint8_t b[0x30];
        if (!api->read_mem(begin + i * 4, &h, 4) || !(it = api->resolve_item(h))) continue;
        if (!api->read_mem(it, b, sizeof b)) continue;
        cm_part *p = &s->p[s->n];
        memset(p, 0, sizeof *p);
        p->addr = it;
        p->loc = cm_i32(b, ITEM_LOCATION);
        p->hp = cm_i32(b, ITEM_INTEGRITY);
        p->active = p->loc <= LOC_WEAPON && cm_i32(b, ITEM_ACTIVE_SINCE) != -1;
        if (!cm_read_def(api, (uint32_t)cm_i32(b, ITEM_DEF_PTR), p)) continue;
        s->n++;
    }
    cm_read_location(api, &s->depth, &s->map_type, s->map_name, sizeof s->map_name);
    uint32_t world = 0;
    int32_t t = 0;
    if (api->read_mem(ADDR_WORLD_PTR, &world, 4) && world && api->read_mem(world + WORLD_GAME_TIME, &t, 4)) s->time = t;
    cm_read_events(api, s);
    if (s->time > s->now_turn) s->now_turn = s->time;
    return 1;
}

static inline int cm_attached(const cm_part *p) { return p->loc >= LOC_POWER && p->loc <= LOC_WEAPON; }
static inline int cm_in_bag(const cm_part *p)   { return p->loc == LOC_INVENTORY; }
static inline int cm_on(const cm_part *p)       { return cm_attached(p) && p->active; }

/* Heat dissipation of cooling parts: game data that lives in an effect sub-object, not a plain
 * ItemDef field: generated from Cog-Minder items.json ("Dissipates N heat per turn"), Beta 17.1. */
static const struct { const char *name; int dissipation; } CM_COOLING[] = {
    { "Heat Sink", 10 },
    { "Imp. Heat Sink", 14 },
    { "Mak. Heat Sink", 19 },
    { "Adv. Heat Sink", 19 },
    { "Exp. Heat Sink", 26 },
    { "Cep. Phasing Heat Sink", 36 },
    { "Cooling System", 22 },
    { "Imp. Cooling System", 27 },
    { "Adv. Cooling System", 31 },
    { "Exp. Cooling System", 38 },
    { "Sfc. Cooling Unit", 35 },
    { "Mak. Coolant Network", 110 },
    { "Coolant Network", 90 },
    { "Asb. Nanovents", 15 },
    { "2N-1CE's Frost Array", 100 },
    { "Disposable Heat Sink", 50 },
    { "Coolant Injector", 65 },
    { "Imp. Coolant Injector", 80 },
    { "Adv. Coolant Injector", 100 },
    { "Exp. Coolant Injector", 120 },
    { "Active Cooling Armor", 60 },
};
static inline int cm_cooling_of(const char *name) {
    for (size_t i = 0; i < sizeof CM_COOLING / sizeof *CM_COOLING; ++i)
        if (!strcmp(CM_COOLING[i].name, name)) return CM_COOLING[i].dissipation;
    return 0;
}

/* ---------------- build math ----------------
 * Counts switched-on parts only. Volley time = Cogmind's base (1 weapon 200, 2 = 300, +25 each up to
 * 6 = 400) plus each active weapon's delay. Movement: only one propulsion type drives (the one with
 * the most support among active parts); a step takes its average time/move plus, when overweight,
 * (ceil(mass/support) - 1) x its average penalty (wiki rule; reproduces the HUD's 950 for core-only
 * movement at mass 56 and 115 for a Huge Wheel at 38/28). Every active propulsion part spends its
 * energy/move, and per-turn income/upkeep/heat run during the step (the HUD's second numbers). */
typedef struct {
    int used[4], bag[4], mass, armor_worn;
    int income; float upkeep, net;             /* energy per turn */
    int heat_gen, heat_dissipation, heat_net;  /* per turn; dissipation = base + active cooling parts */
    int prop_type, prop_n, support, overweight; /* overweight = mass - support (>0 = over) */
    int times_overweight;                      /* ceil(mass/support) - 1 when over, else 0 */
    float time_per_move, energy_per_move, energy_per_step, heat_per_step; int heat_per_move;
    int weapons, volley_time, volley_matter, volley_energy, volley_heat;
    int volleys_left;                          /* by matter and energy; -1 = no ammo cost */
} cm_math;

static inline void cm_compute(const cm_state *s, cm_math *m) {
    int support[IT_COUNT] = { 0 }, prop_n[IT_COUNT] = { 0 }, tpm[IT_COUNT] = { 0 }, hpm[IT_COUNT] = { 0 };
    float move_e[IT_COUNT] = { 0 };
    int delay = 0;
    int pen[IT_COUNT] = { 0 };
    memset(m, 0, sizeof *m);
    m->income = s->innate_energy_gen;
    m->heat_dissipation = s->base_heat_dissipation;
    m->prop_type = -1;
    for (int i = 0; i < s->n; ++i) {
        const cm_part *p = &s->p[i];
        if (cm_in_bag(p)) { if (p->slot >= 0) m->bag[p->slot]++; continue; }
        if (!cm_attached(p)) continue;
        m->used[p->loc] += p->size;
        m->mass += p->mass;
        if (p->type == IT_PROTECTION) m->armor_worn++;
        if (!p->active) continue;
        m->upkeep += p->upkeep;
        m->heat_gen += p->heat_gen;
        if (p->loc == LOC_UTILITY) m->heat_dissipation += cm_cooling_of(p->name);
        if (p->loc == LOC_POWER) m->income += p->energy_gen;
        if (p->loc == LOC_WEAPON) {
            m->weapons++;
            m->volley_energy += p->shot_energy; m->volley_matter += p->shot_matter; m->volley_heat += p->shot_heat;
            delay += p->delay;
        }
        if (p->loc == LOC_PROPULSION && p->type >= IT_TREADS && p->type <= IT_FLIGHT) {
            support[p->type] += p->support; prop_n[p->type]++; tpm[p->type] += p->time_per_move;
            move_e[p->type] += p->energy_per_move; hpm[p->type] += p->heat_per_move; pen[p->type] += p->penalty;
        }
    }
    m->net = m->income - m->upkeep;
    m->heat_net = m->heat_gen - m->heat_dissipation;
    for (int t = IT_TREADS; t <= IT_FLIGHT; ++t)
        if (prop_n[t] && (m->prop_type < 0 || support[t] > support[m->prop_type])) m->prop_type = t;
    float base_t, penalty;
    if (m->prop_type >= 0) {
        int t = m->prop_type;
        m->prop_n = prop_n[t]; m->support = support[t];
        base_t = (float)tpm[t] / prop_n[t];
        penalty = (float)pen[t] / prop_n[t];
        m->energy_per_move = move_e[t]; m->heat_per_move = hpm[t];
    } else {                                           /* core-only movement (wiki + HUD: 50, penalty 50, 1 energy) */
        m->support = 3; base_t = 50; penalty = 50; m->energy_per_move = 1;
    }
    m->overweight = m->mass - m->support;
    if (m->overweight > 0 && m->support > 0) m->times_overweight = (m->mass + m->support - 1) / m->support - 1;
    m->time_per_move = base_t + m->times_overweight * penalty;
    m->energy_per_step = m->net * m->time_per_move / 100.0f - m->energy_per_move;
    m->heat_per_step = m->heat_net * m->time_per_move / 100.0f + m->heat_per_move;
    if (m->weapons) {
        int base = m->weapons == 1 ? 200 : m->weapons >= 6 ? 400 : 300 + 25 * (m->weapons - 2);
        m->volley_time = base + delay;
    }
    m->volleys_left = -1;
    if (m->volley_matter > 0) m->volleys_left = s->matter / m->volley_matter;
    if (m->volley_energy > 0) {
        int e = s->energy / m->volley_energy;
        if (m->volleys_left < 0 || e < m->volleys_left) m->volleys_left = e;
    }
}

/* ---------------- damage taken, from the combat log ----------------
 * Combat-log shapes (Beta 17.1):
 *   "05053_  P-60: Backup Laser I (66%) Hit"      an enemy attack ("<attacker>: <weapon> (...) Hit|Miss")
 *   "05054_   Adv. Assault Cannon damaged: 9"     damage to one of YOUR parts (no robot designation)
 *   "05056_   P-60 core damaged: 33"              damage to a robot (starts with a designation)
 *   "05053_  Adv. Assault Cannon (57-1=56%) Hit"  your own attack (no "name:" prefix)
 * Your core shows as "core damaged: N" without a designation. Damage is attributed to the most
 * recent enemy attack line. */
#define CM_DMG_MAX 16
typedef struct { char name[48]; int dmg, hits; } cm_dmg_row;
typedef struct {
    int from_turn, to_turn, total, core;
    int n_src, n_part;
    cm_dmg_row src[CM_DMG_MAX], part[CM_DMG_MAX];
} cm_damage;

static inline int cm_is_designation(const char *t) {   /* "P-60 ", "G-47 ", "Y-54 " ... */
    int i = 0;
    while (t[i] >= 'A' && t[i] <= 'Z') ++i;
    if (i < 1 || i > 3 || t[i] != '-') return 0;
    int j = i + 1;
    while (t[j] >= '0' && t[j] <= '9') ++j;
    return j > i + 1 && t[j] == ' ';
}
static inline void cm_dmg_add(cm_dmg_row *rows, int *n, const char *name, size_t len, int dmg) {
    if (len >= sizeof rows[0].name) len = sizeof rows[0].name - 1;
    for (int i = 0; i < *n; ++i)
        if (strlen(rows[i].name) == len && !strncmp(rows[i].name, name, len)) { rows[i].dmg += dmg; rows[i].hits++; return; }
    if (*n >= CM_DMG_MAX) return;
    memcpy(rows[*n].name, name, len);
    rows[*n].name[len] = 0;
    rows[*n].dmg = dmg; rows[*n].hits = 1;
    (*n)++;
}
static inline void cm_dmg_sort(cm_dmg_row *r, int n) {
    for (int i = 1; i < n; ++i) for (int j = i; j > 0 && r[j].dmg > r[j - 1].dmg; --j) { cm_dmg_row t = r[j]; r[j] = r[j - 1]; r[j - 1] = t; }
}

/* msgs: combat log, oldest first. Counts messages with turn >= since_turn. */
static inline void cm_damage_taken(const cm_msg *msgs, int n, int since_turn, cm_damage *d) {
    char attacker[48] = "";
    memset(d, 0, sizeof *d);
    d->from_turn = since_turn;
    for (int i = 0; i < n; ++i) {
        const char *t = msgs[i].text;
        const char *u = strchr(t, '_');                     /* skip the "05053_" turn prefix */
        t = u && u - t <= 6 ? u + 1 : t;
        while (*t == ' ') ++t;
        if (msgs[i].turn < since_turn) continue;
        if (msgs[i].turn > d->to_turn) d->to_turn = msgs[i].turn;
        const char *colon = strstr(t, ": ");
        const char *dam = strstr(t, " damaged: ");
        if (!dam && (strstr(t, ") Hit") || strstr(t, ") Miss"))) {      /* an attack line */
            const char *paren = strchr(t, '(');
            if (colon && paren && colon < paren) {          /* "<attacker>: <weapon> (...)" */
                size_t l = (size_t)(colon - t);
                if (l >= sizeof attacker) l = sizeof attacker - 1;
                memcpy(attacker, t, l); attacker[l] = 0;
            } else attacker[0] = 0;                          /* your own shot */
            continue;
        }
        if (!dam || !attacker[0] || cm_is_designation(t)) continue;
        int v = atoi(dam + 10);
        if (v <= 0) continue;
        d->total += v;
        if (!strncmp(t, "core", 4) || !strncmp(t, "Core", 4)) d->core += v;
        cm_dmg_add(d->src, &d->n_src, attacker, strlen(attacker), v);
        cm_dmg_add(d->part, &d->n_part, t, (size_t)(dam - t), v);
    }
    cm_dmg_sort(d->src, d->n_src);
    cm_dmg_sort(d->part, d->n_part);
}
#endif
