// SPDX-License-Identifier: MPL-2.0
// replay.exe <snapshot.dmp>: run the plugin-side code (include/cm_state.h, cm_ui.h, cm_map.h: state,
// logs, build math, UI, floor) against a Ctrl+F11 snapshot instead of the live game. The host API is emulated on
// top of the dump: reads come from the dump, game-code calls (max energy/matter) return 0.
// Useful to check offsets after a game update without starting a run.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cm_state.h"
#include "cm_ui.h"
#include "cm_map.h"

typedef struct { uint64_t va, size; uint64_t off; } region;
static uint8_t *g_file;
static region *g_reg;
static int g_nreg;

static int load_dump(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    g_file = malloc((size_t)n);
    if (!g_file || fread(g_file, 1, (size_t)n, f) != (size_t)n) { fclose(f); return 0; }
    fclose(f);
    if (memcmp(g_file, "MDMP", 4)) return 0;
    uint32_t ns = *(uint32_t *)(g_file + 8), dir = *(uint32_t *)(g_file + 12);
    for (uint32_t i = 0; i < ns; ++i) {
        uint32_t type = *(uint32_t *)(g_file + dir + i * 12), rva = *(uint32_t *)(g_file + dir + i * 12 + 8);
        if (type == 5) {                                       // MemoryListStream
            uint32_t cnt = *(uint32_t *)(g_file + rva);
            g_reg = realloc(g_reg, sizeof(region) * (g_nreg + cnt));
            for (uint32_t j = 0; j < cnt; ++j) {
                uint8_t *d = g_file + rva + 4 + j * 16;
                g_reg[g_nreg++] = (region){ *(uint64_t *)d, *(uint32_t *)(d + 8), *(uint32_t *)(d + 12) };
            }
        } else if (type == 9) {                                // Memory64ListStream (full dumps)
            uint64_t cnt = *(uint64_t *)(g_file + rva), off = *(uint64_t *)(g_file + rva + 8);
            g_reg = realloc(g_reg, sizeof(region) * (g_nreg + cnt));
            for (uint64_t j = 0; j < cnt; ++j) {
                uint8_t *d = g_file + rva + 16 + j * 16;
                g_reg[g_nreg++] = (region){ *(uint64_t *)d, *(uint64_t *)(d + 8), off };
                off += *(uint64_t *)(d + 8);
            }
        }
    }
    return g_nreg > 0;
}

// Snapshots hold heap + writable data only; read-only image data (RTTI names, string tables) comes
// from COGMIND.exe on disk, mapped by its section table.
static uint8_t *g_exe;
static struct { uint32_t va, size, off; } g_sec[16];
static int g_nsec;
static void load_exe(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    g_exe = malloc((size_t)n);
    if (!g_exe || fread(g_exe, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(g_exe); g_exe = NULL; return; }
    fclose(f);
    uint32_t pe = *(uint32_t *)(g_exe + 0x3c), base = *(uint32_t *)(g_exe + pe + 24 + 28);
    uint16_t ns = *(uint16_t *)(g_exe + pe + 6), opt = *(uint16_t *)(g_exe + pe + 20);
    for (int i = 0; i < ns && g_nsec < 16; ++i) {
        uint8_t *sh = g_exe + pe + 24 + opt + i * 40;
        uint32_t vs = *(uint32_t *)(sh + 8), va = *(uint32_t *)(sh + 12), rs = *(uint32_t *)(sh + 16), ro = *(uint32_t *)(sh + 20);
        g_sec[g_nsec].va = base + va; g_sec[g_nsec].size = vs < rs ? vs : rs; g_sec[g_nsec].off = ro; ++g_nsec;
    }
}
static int exe_read(uint32_t a, uint8_t *o, size_t n) {
    for (int i = 0; i < g_nsec; ++i)
        if (a >= g_sec[i].va && a + n <= g_sec[i].va + g_sec[i].size) { memcpy(o, g_exe + g_sec[i].off + (a - g_sec[i].va), n); return 1; }
    return 0;
}

static int __cdecl d_read(uint32_t a, void *out, size_t n) {
    uint8_t *o = out;
    while (n) {
        int hit = 0;
        for (int i = 0; i < g_nreg; ++i) {
            if (a < g_reg[i].va || a >= g_reg[i].va + g_reg[i].size) continue;
            size_t k = (size_t)(g_reg[i].va + g_reg[i].size - a);
            if (k > n) k = n;
            memcpy(o, g_file + g_reg[i].off + (a - g_reg[i].va), k);
            o += k; a += (uint32_t)k; n -= k; hit = 1;
            break;
        }
        if (!hit) return exe_read(a, o, n);
    }
    return 1;
}
static uint32_t r32(uint32_t a) { uint32_t v = 0; d_read(a, &v, 4); return v; }
static int __cdecl d_string(uint32_t a, char *out, size_t cap) {     // MSVC10 std::string
    uint8_t s[28];
    if (!cap || !d_read(a, s, sizeof s)) return 0;
    uint32_t size = *(uint32_t *)(s + 16), res = *(uint32_t *)(s + 20);
    if (size > res || size > 4096) return 0;
    size_t n = size < cap - 1 ? size : cap - 1;
    if (res < 16) memcpy(out, s, n);
    else if (!d_read(*(uint32_t *)s, out, n)) return 0;
    out[n] = 0;
    return 1;
}
static uint32_t resolve(uint32_t reg_global, uint32_t h) {
    uint32_t reg = r32(reg_global), idx = h & 0xffff, gen = h >> 16, v[2];
    if (!h || !reg || !d_read(reg, v, 8) || idx >= (v[1] - v[0]) / 4) return 0;
    if ((r32(r32(reg + 0x10) + 4 * idx)) != gen) return 0;
    return r32(v[0] + 4 * idx);
}
static uint32_t __cdecl d_res_item(uint32_t h) { return resolve(ADDR_ITEM_REGISTRY, h); }
static uint32_t __cdecl d_res_ent(uint32_t h)  { return resolve(ADDR_ENT_REGISTRY, h); }
static uint32_t __cdecl d_player(void) { uint32_t w = r32(ADDR_WORLD_PTR); return w ? d_res_ent(r32(w + WORLD_PLAYER_HANDLE)) : 0; }
static uint32_t vec_at(uint32_t g, uint32_t i) { uint32_t v[2]; return d_read(g, v, 8) && i < (v[1] - v[0]) / 4 ? r32(v[0] + 4 * i) : 0; }
static uint32_t __cdecl d_item_def(uint32_t i) { return vec_at(ADDR_ITEM_DEFS, i); }
static uint32_t __cdecl d_bot_def(uint32_t i)  { return vec_at(ADDR_BOT_DEFS, i); }
static int __cdecl d_max_core(void) { return (int)r32(ADDR_PLAYER_MAX_CORE); }
static int __cdecl d_zero(void) { return 0; }

static void ui_tree(uint32_t a, int depth, int all) {
    cm_ui_node n;
    if (depth > 16 || !cm_ui_node_read(d_read, a, &n) || (n.hidden && !all)) return;
    char t[200];
    cm_ui_row_text(d_read, &n, 0, t, sizeof t);
    printf("%*s%s%s %d,%d %dx%d  %.90s\n", depth * 2, "", n.hidden ? "(hidden) " : "", n.cls, n.ax, n.ay, n.rw, n.rh, t);
    for (uint32_t i = 0; i < n.nkids; ++i) ui_tree(cm_ui_child(d_read, &n, i), depth + 1, all);
}

static void ui_report(int full) {
    cm_mouse m;
    char b[256];
    if (!cm_ui_mouse(d_read, &m)) { puts("ui: no mouse state"); return; }
    cm_ui_describe(d_read, m.hovered, b, sizeof b);
    printf("mouse px %d,%d cell %d,%d  hovered: %s\n", m.px, m.py, m.cx, m.cy, b);
    cm_ui_describe(d_read, cm_ui_at(d_read, m.cx, m.cy), b, sizeof b);
    printf("under cursor: %s\n", b);
    int wx, wy;
    if (cm_ui_map_tile(d_read, m.cx, m.cy, &wx, &wy)) printf("map tile under cursor: %d,%d\n", wx, wy);
    uint32_t parts = cm_ui_find(d_read, cm_ui_root(d_read), "CParts", 1);
    cm_ui_describe(d_read, parts, b, sizeof b);
    printf("find CParts: %s\n", b);
    static char scr[64 * 1024];
    cm_screen_text(d_read, 154, 0, 60, 9, scr, sizeof scr);
    printf("screen 154,0 60x9 (HUD):\n%s\n", scr);
    if (!full) return;
    puts("ui tree (visible):");
    ui_tree(cm_ui_root(d_read), 0, 0);
    cm_screen_text(d_read, 0, 0, 0, 0, scr, sizeof scr);
    printf("screen:\n%s\n", scr);
}

int main(int argc, char **argv) {
    int ui_full = argc > 2 && !strcmp(argv[2], "--ui");
    if (argc < 2 || !load_dump(argv[1])) { fprintf(stderr, "usage: replay <snapshot.dmp> [--ui [COGMIND.exe]]  (could not load)\n"); return 2; }
    {   // COGMIND.exe: argv[3], else two folders up from hack\snapshots\snap_N.dmp
        char exe[1024];
        if (argc > 3) snprintf(exe, sizeof exe, "%s", argv[3]);
        else {
            snprintf(exe, sizeof exe, "%s", argv[1]);
            for (int up = 0; up < 3; ++up) { char *s = strrchr(exe, '\\'); if (!s) s = strrchr(exe, '/'); if (s) *s = 0; else { exe[0] = 0; break; } }
            if (exe[0]) strncat(exe, "\\COGMIND.exe", sizeof exe - strlen(exe) - 1);
        }
        if (exe[0]) load_exe(exe);
    }
    ui_report(ui_full);
    cm_host_api api;
    memset(&api, 0, sizeof api);
    api.version = 3;
    api.read_mem = d_read; api.read_string = d_string; api.player = d_player;
    api.resolve_entity = d_res_ent; api.resolve_item = d_res_item;
    api.item_def = d_item_def; api.bot_def = d_bot_def;
    api.max_core = d_max_core; api.read_string = d_string; api.max_energy = d_zero; api.max_matter = d_zero;

    static cm_state st;
    if (!cm_read_state(&api, &st)) { puts("no run in this snapshot"); return 1; }
    printf("core %d/%d  energy %d  matter %d  corruption %d%%  @(%d,%d)  slots %d/%d/%d/%d  last msg turn %d\n",
           st.core, st.max_core, st.energy, st.matter, st.corruption, st.x, st.y,
           st.slots[0], st.slots[1], st.slots[2], st.slots[3], st.now_turn);
    for (int i = 0; i < st.n; ++i) {
        const cm_part *p = &st.p[i];
        printf("  %-10s %-26s %-16s fits %-10s size %d  %3d/%-3d mass %2d  sup %2d  upkeep %.0f  e/move %.0f\n",
               cm_attached(p) ? (p->active ? cm_slot_name(p->loc) : "off") : "bag", p->name, cm_type_name(p->type), cm_slot_name(p->slot),
               p->size, p->hp, p->max_hp, p->mass, p->support, p->upkeep, p->energy_per_move);
    }
    static cm_msg m[8];
    int n = cm_read_log(&api, ADDR_MSG_LOG, 8, m);
    puts("message log (last 8):");
    for (int i = 0; i < n; ++i) printf("  [%d] %s\n", m[i].turn, m[i].text);
    n = cm_read_log(&api, ADDR_COMBAT_LOG, 3, m);
    puts("combat log (last 3):");
    for (int i = 0; i < n; ++i) printf("  [%d] %s\n", m[i].turn, m[i].text);
    cm_math mm;
    cm_compute(&st, &mm);
    printf("location -%d/%s (map type %d)\n", st.depth, st.map_name, st.map_type);
    printf("heat %d: +%d gen, -%d dissipation (%d evolutions) = %+d/turn, %+.1f/step\n", st.heat, mm.heat_gen,
           mm.heat_dissipation, st.evolutions, mm.heat_net, mm.heat_per_step);
    printf("time %d | energy +%d -%.0f = %+.1f/turn | %s x%d: mass %d support %d (over %d, x%d), %.0f time/move, %.0f e/move, %+.1f e/step\n"
           "volley: %d weapons, time %d, matter %d, energy %d, heat %d, volleys left %d | heat gen %d/turn\n",
           st.time, mm.income, mm.upkeep, mm.net, mm.prop_type >= 0 ? cm_type_name(mm.prop_type) : "core", mm.prop_n,
           mm.mass, mm.support, mm.overweight, mm.times_overweight, mm.time_per_move, mm.energy_per_move, mm.energy_per_step,
           mm.weapons, mm.volley_time, mm.volley_matter, mm.volley_energy, mm.volley_heat, mm.volleys_left, mm.heat_gen);
    static cm_msg cl[1000];
    static cm_damage dmg;
    int cn = cm_read_log(&api, ADDR_COMBAT_LOG, 1000, cl);
    cm_damage_taken(cl, cn, st.now_turn - 300, &dmg);
    printf("damage taken, turns %d..%d: total %d, core %d\n", dmg.from_turn, dmg.to_turn, dmg.total, dmg.core);
    for (int i = 0; i < dmg.n_src && i < 6; ++i) printf("  from %-22s %4d (%d hits)\n", dmg.src[i].name, dmg.src[i].dmg, dmg.src[i].hits);
    for (int i = 0; i < dmg.n_part && i < 6; ++i) printf("  to   %-22s %4d (%d hits)\n", dmg.part[i].name, dmg.part[i].dmg, dmg.part[i].hits);
    static cm_floor fl;
    if (cm_read_floor(&api, &fl)) {
        printf("floor %dx%d, %d known tiles, player %d,%d: %d known items, %d known machines\n", fl.w, fl.h,
               fl.known_tiles, fl.px, fl.py, fl.n_items, fl.n_machines);
        for (int i = 0; i < fl.n_items && i < 12; ++i)
            printf("  item  %-28s %3d,%-3d dist %2d  %d/%d\n", fl.items[i].name, fl.items[i].x, fl.items[i].y, fl.items[i].dist, fl.items[i].hp, fl.items[i].max_hp);
        for (int i = 0; i < fl.n_machines; ++i)
            printf("  mach  %-28s %-16s %3d,%-3d dist %2d  (%d tiles)\n", fl.machines[i].name, fl.machines[i].type, fl.machines[i].x, fl.machines[i].y, fl.machines[i].dist, fl.machines[i].tiles);
    }
    return 0;
}
