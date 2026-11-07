// SPDX-License-Identifier: MPL-2.0
// Ctrl+F10: human-readable dump of the player, their items and every entity on the level.
#include <stdio.h>
#include <stdlib.h>
#include "cogmind.h"
#include "base/mem.h"
#include "debug/snapshot.h"
#include "game/player.h"

void dump_state(const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return;
    player_info pi;
    char nm[96];
    if (!read_player(&pi)) {
        fprintf(f, "no player (title screen / loading?)\n");
        fclose(f);
        return;
    }
    uint32_t p = pi.addr;
    fprintf(f, "PLAYER @%08x  core %d/%d  energy %d/%d  matter %d/%d  corruption %d%%  core-cov %d  pos (%d,%d)\n\n",
            p, pi.core, player_max_core(), pi.energy, player_max_energy(), pi.matter, player_max_matter(),
            pi.corruption, pi.core_coverage, pi.x, pi.y);
    fprintf(f, "SLOTS  power %d  propulsion %d  utility %d  weapon %d\n\n",
            rdi(p + ENT_SLOTS_POWER), rdi(p + ENT_SLOTS_PROP), rdi(p + ENT_SLOTS_UTIL), rdi(p + ENT_SLOTS_WEAPON));

    static const char *LOC[] = { "power", "prop", "util", "weapon", "inv", "floor", "?6", "?7" };
    fprintf(f, "ITEMS (%u)\n", pi.n_items);
    fprintf(f, "  %-6s %-30s %9s %4s %4s %5s %5s %6s %8s\n", "slot", "name", "integrity", "rtg", "size", "mass", "cover", "range", "damage");
    // equipped grouped by slot type (HUD order), then inventory in its own order
    for (int want = LOC_POWER; want <= LOC_INVENTORY; ++want) {
        int inv_n = 0;
        for (uint32_t i = 0; i < pi.n_items && i < 64; ++i) {
            uint32_t it = resolve(ADDR_ITEM_REGISTRY, rd32(pi.items_vec + i * 4));
            if (!it || (int)rd32(it + ITEM_LOCATION) != want) continue;
            uint32_t d = rd32(it + ITEM_DEF_PTR);
            rd_string(d + IDEF_NAME, nm, sizeof nm);
            char integ[24], dmg[24] = "", slot[16];
            snprintf(integ, sizeof integ, "%d/%d", rdi(it + ITEM_INTEGRITY), rdi(d + IDEF_INTEGRITY));
            if (rdi(d + IDEF_DAMAGE_MAX)) snprintf(dmg, sizeof dmg, "%d-%d", rdi(d + IDEF_DAMAGE_MIN), rdi(d + IDEF_DAMAGE_MAX));
            if (want == LOC_INVENTORY) snprintf(slot, sizeof slot, "inv %d", ++inv_n);
            else snprintf(slot, sizeof slot, "%s", LOC[want]);
            fprintf(f, "  %-6s %-30s %9s %4d %4d %5d %5d %6d %8s\n", slot, nm, integ, rdi(d + IDEF_RATING), rdi(d + IDEF_SIZE),
                    rdi(d + IDEF_MASS), rdi(d + IDEF_COVERAGE), rdi(d + IDEF_RANGE), dmg);
        }
    }

    uint32_t n = registry_count(ADDR_ENT_REGISTRY);
    fprintf(f, "\nENTITIES (%u slots)\n", n);
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t e = registry_slot(ADDR_ENT_REGISTRY, i);
        if (!e || !rd_string(e + ENT_NAME, nm, sizeof nm)) continue;
        int ex = rdi(e + ENT_POS_X), ey = rdi(e + ENT_POS_Y);
        int dx = abs(ex - pi.x), dy = abs(ey - pi.y);
        fprintf(f, "  #%-4u %-24s core %4d  pos (%3d,%3d)  dist %3d\n", i, nm, rdi(e + ENT_CORE), ex, ey, dx > dy ? dx : dy);
    }
    fclose(f);
}
