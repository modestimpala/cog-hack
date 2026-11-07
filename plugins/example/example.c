/* SPDX-License-Identifier: MIT */
/* Example cog-hack plugin. Build (any 32-bit compiler):
 *   make                     (-> build/plugins/example.dll)
 *   i686-w64-mingw32-gcc -O2 -I../../include -shared -s -o example.dll example.c
 *   cl /LD /O2 /I..\..\include example.c      (MSVC x86)
 * Drop it into <game>\hack\plugins\ and enable it in hack\plugins.txt.
 */
#include <stdio.h>
#include <string.h>
#include "plugin_api.h"   /* include/: the only header a plugin needs */

static const cm_host_api *api;

/* int add(int a, int b) */
static void __cdecl fn_add(cm_call *c) { api->ret_int(c, api->arg_int(c, 0) + api->arg_int(c, 1)); }

/* string shout(const string &in s) */
static void __cdecl fn_shout(cm_call *c) {
    char buf[256];
    snprintf(buf, sizeof buf, "%s!", api->arg_string(c, 0));
    for (char *p = buf; *p; ++p) if (*p >= 'a' && *p <= 'z') *p -= 32;
    api->ret_string(c, buf);
}

/* int raw_core(): the player's core integrity, read from game memory through the host API
 * (0 when not in a run). Shows plugin-side game access. */
static void __cdecl fn_raw_core(cm_call *c) {
    uint32_t p = api->player();
    int32_t v = 0;
    if (p) api->read_mem(p + 0x8c, &v, 4);
    api->ret_int(c, v);
}

/* double safe_div(double a, double b): demonstrates raising a script exception */
static void __cdecl fn_safe_div(cm_call *c) {
    double b = api->arg_double(c, 1);
    if (b == 0) { api->set_exception(c, "safe_div: division by zero"); return; }
    api->ret_double(c, api->arg_double(c, 0) / b);
}

/* uint game_addr(const string &in name): a game address as the host resolved it for this build
 * Plugins using cogmind.h get all of them at once with cm_addr_init(api), cm_addr.h. */
static void __cdecl fn_game_addr(cm_call *c) { api->ret_int(c, (int)api->addr(api->arg_string(c, 0))); }

__declspec(dllexport) int __cdecl cogmind_plugin_init(const cm_host_api *host) {
    if (host->version < CM_API_VERSION) return -1;
    api = host;
    api->register_function("int add(int, int)", fn_add);
    api->register_function("string shout(const string &in)", fn_shout);
    api->register_function("int raw_core()", fn_raw_core);
    api->register_function("double safe_div(double, double)", fn_safe_div);
    api->register_function("uint game_addr(const string &in)", fn_game_addr);
    api->log("example plugin ready: add() shout() raw_core() safe_div()");
    return 0;
}
