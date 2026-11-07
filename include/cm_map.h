/* SPDX-License-Identifier: MIT */
/* cm_map.h: the current floor as the player knows it (header-only, plugin side): which tiles are
 * known, the items lying on known tiles and the machines with a known piece, with distances.
 *
 *   static cm_floor f;
 *   cm_read_floor(api, &f);      // f.items[], f.machines[] sorted by distance from the player
 *
 * Spoiler rules: only tiles the player's map memory has (the same tiles the map view draws).
 * Item names follow the game's naming: unidentified prototypes read "Prototype Weapon" etc.
 * Traps are never listed (hidden until discovered; their discovered state isn't mapped).
 * Caveat: item positions are the live ones, so an item that moved after you saw it shows where it
 * is now. Distance = moves (Chebyshev). Read-only; call on the game thread.
 */
#ifndef CM_MAP_H
#define CM_MAP_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "plugin_api.h"
#include "cogmind.h"
#include "cm_addr.h"

#define CM_FLOOR_MAX_ITEMS    256
#define CM_FLOOR_MAX_MACHINES 128
#define CM_FLOOR_MAX_PROPS    2048

typedef struct { char name[64]; int x, y, dist, hp, max_hp; } cm_floor_item;
typedef struct { char name[64], type[32]; int x, y, dist, tiles; } cm_floor_machine;   /* x,y = nearest known piece */
typedef struct {
    int ok, w, h, known_tiles, px, py;
    int n_items, n_machines;
    cm_floor_item items[CM_FLOOR_MAX_ITEMS];
    cm_floor_machine machines[CM_FLOOR_MAX_MACHINES];
} cm_floor;

static inline uint32_t cm_m32(const cm_host_api *api, uint32_t a) { uint32_t v = 0; api->read_mem(a, &v, 4); return v; }

/* display name of an item def the way the game shows it (FUN_0055fab0) */
static inline void cm_item_display_name(const cm_host_api *api, uint32_t def, char *out, size_t cap) {
    int32_t kind = (int32_t)cm_m32(api, def + IDEF_PROTO_KIND);
    uint32_t id = cm_m32(api, def + IDEF_ID), v[2] = { 0, 0 };
    int identified = 1;
    if (kind >= 0 && kind <= 3 && api->read_mem(ADDR_ITEM_IDENTIFIED, v, 8) && v[1] > v[0] && id < (v[1] - v[0]) / 4)
        identified = cm_m32(api, v[0] + 4 * id) != 0;
    if (identified) { if (!api->read_string(def + IDEF_NAME, out, cap)) snprintf(out, cap, "?"); return; }
    static const char *PRE[4] = { "Unknown ", "Prototype ", "Construct ", "Alien " };
    char type[40] = "item";
    api->read_string(ADDR_ITEMTYPE_NAMES + 0x1c * cm_m32(api, def + IDEF_TYPE), type, sizeof type);
    snprintf(out, cap, "%s%s", PRE[kind], type);
}

static inline int cm_chebyshev(int ax, int ay, int bx, int by) {
    int dx = abs(ax - bx), dy = abs(ay - by);
    return dx > dy ? dx : dy;
}

static int cm_floor_cmp_item(const void *a, const void *b) { return ((const cm_floor_item *)a)->dist - ((const cm_floor_item *)b)->dist; }
static int cm_floor_cmp_mach(const void *a, const void *b) { return ((const cm_floor_machine *)a)->dist - ((const cm_floor_machine *)b)->dist; }

static inline int cm_read_floor(const cm_host_api *api, cm_floor *f) {
    static uint8_t known[512 * 512];
    memset(f, 0, sizeof *f);
    uint32_t pl = api->player(), world = cm_m32(api, ADDR_WORLD_PTR);
    if (!pl || !world) return 0;
    f->px = (int32_t)cm_m32(api, pl + ENT_POS_X); f->py = (int32_t)cm_m32(api, pl + ENT_POS_Y);
    f->w = (int32_t)cm_m32(api, world + WORLD_KNOWN_W); f->h = (int32_t)cm_m32(api, world + WORLD_KNOWN_H);
    if (f->w <= 0 || f->h <= 0 || f->w > 512 || f->h > 512 || !api->read_mem(cm_m32(api, world + WORLD_KNOWN), known, (size_t)(f->w * f->h))) return 0;
    for (int i = 0; i < f->w * f->h; ++i) f->known_tiles += known[i] != 0;
#define KNOWN(x, y) ((x) >= 0 && (y) >= 0 && (x) < f->w && (y) < f->h && known[(x) * f->h + (y)])

    /* items on the floor */
    uint32_t reg = cm_m32(api, ADDR_ITEM_REGISTRY), v[2];
    if (reg && api->read_mem(reg, v, 8) && v[1] > v[0] && (v[1] - v[0]) / 4 <= 0x10000)
        for (uint32_t k = 0; k < (v[1] - v[0]) / 4 && f->n_items < CM_FLOOR_MAX_ITEMS; ++k) {
            uint32_t it = cm_m32(api, v[0] + 4 * k);
            int32_t b[8];
            if (!it || !api->read_mem(it, b, sizeof b) || b[ITEM_LOCATION / 4] != LOC_FLOOR) continue;
            int x = b[ITEM_X / 4], y = b[ITEM_Y / 4];
            if (!KNOWN(x, y)) continue;
            cm_floor_item *o = &f->items[f->n_items++];
            uint32_t def = (uint32_t)b[ITEM_DEF_PTR / 4];
            cm_item_display_name(api, def, o->name, sizeof o->name);
            o->x = x; o->y = y; o->dist = cm_chebyshev(x, y, f->px, f->py);
            o->hp = b[ITEM_INTEGRITY / 4]; o->max_hp = (int32_t)cm_m32(api, def + IDEF_INTEGRITY);
        }

    /* machines: props grouped into connected pieces of the same def; listed if any piece is known */
    static struct { uint32_t def; int x, y, group; } pr[CM_FLOOR_MAX_PROPS];
    int np = 0;
    reg = cm_m32(api, ADDR_PROP_REGISTRY);
    if (reg && api->read_mem(reg, v, 8) && v[1] > v[0] && (v[1] - v[0]) / 4 <= 0x10000)
        for (uint32_t k = 0; k < (v[1] - v[0]) / 4 && np < CM_FLOOR_MAX_PROPS; ++k) {
            uint32_t p = cm_m32(api, v[0] + 4 * k), b[4];
            if (!p || !api->read_mem(p, b, sizeof b) || !b[PROP_DEF / 4]) continue;
            pr[np].def = b[PROP_DEF / 4]; pr[np].x = (int32_t)b[PROP_X / 4]; pr[np].y = (int32_t)b[PROP_Y / 4]; pr[np].group = -1;
            ++np;
        }
    for (int i = 0; i < np; ++i) {
        if (pr[i].group >= 0) continue;
        /* flood fill this machine (8-connected, same def) */
        static int stack[CM_FLOOR_MAX_PROPS];
        int sp = 0, g = i, any_known = 0, best = -1, bestd = 1 << 30, tiles = 0;
        pr[i].group = g; stack[sp++] = i;
        while (sp) {
            int c = stack[--sp];
            ++tiles;
            if (KNOWN(pr[c].x, pr[c].y)) {
                any_known = 1;
                int dd = cm_chebyshev(pr[c].x, pr[c].y, f->px, f->py);
                if (dd < bestd) { bestd = dd; best = c; }
            }
            for (int j = 0; j < np; ++j)
                if (pr[j].group < 0 && pr[j].def == pr[c].def && abs(pr[j].x - pr[c].x) <= 1 && abs(pr[j].y - pr[c].y) <= 1) {
                    pr[j].group = g; stack[sp++] = j;
                }
        }
        if (!any_known || f->n_machines >= CM_FLOOR_MAX_MACHINES) continue;
        cm_floor_machine *m = &f->machines[f->n_machines];
        api->read_string(pr[i].def + PDEF_NAME, m->name, sizeof m->name);
        api->read_string(pr[i].def + PDEF_TYPE, m->type, sizeof m->type);
        if (strstr(m->name, "Trap") || strstr(m->type, "Trap")) continue;      /* hidden until discovered */
        m->x = pr[best].x; m->y = pr[best].y; m->dist = bestd; m->tiles = tiles;
        ++f->n_machines;
    }
#undef KNOWN
    qsort(f->items, (size_t)f->n_items, sizeof *f->items, cm_floor_cmp_item);
    qsort(f->machines, (size_t)f->n_machines, sizeof *f->machines, cm_floor_cmp_mach);
    f->ok = 1;
    return 1;
}

#endif
