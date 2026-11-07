/* SPDX-License-Identifier: MIT */
/* cm_addr.h: plugin side of the address table (include/cogmind.h).
 *
 * cogmind.h's ADDR_* / FN_* are slots in cm_addr_table; in a plugin that table starts with the
 * values of the build cogmind.h was mapped on. Call cm_addr_init(api) once in cogmind_plugin_init
 * to take the host's values instead: the host re-finds every address in the running build by
 * signature (src/game/version.c), so the plugin follows a game update in which globals moved.
 * Returns how many addresses the host knew.
 */
#ifndef CM_ADDR_H
#define CM_ADDR_H
#include "plugin_api.h"
#include "cogmind.h"

static inline int cm_addr_init(const cm_host_api *api) {
    int n = 0;
    if (!api || !api->addr) return 0;
    for (int i = 0; i < CM_A_COUNT; ++i) {
        uint32_t v = api->addr(cm_addr_names[i]);
        if (v) { cm_addr_table[i] = v; ++n; }
    }
    return n;
}
#endif
