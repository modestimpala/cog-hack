/* SPDX-License-Identifier: MIT */
/* ui_inspect.dll: hover inspector, and the smallest example of building on the game's UI:
 * when the game's hovered element changes (api->on_ui), a label in the game's own font is placed
 * just under it (api->label_set), naming its class and bounds in UI cells: the names and
 * coordinates scripts / plugins / the MCP use (cm_ui.h).
 *
 * Script: ui_inspect(false) turns it off, ui_inspect(true) back on. Read-only.
 */
#include <windows.h>
#include "plugin_api.h"
#include "cm_addr.h"
#include "cm_ui.h"

static const cm_host_api *api;
static int g_on = 1;

static int __cdecl rdm(uint32_t a, void *o, size_t n) { return api->read_mem(a, o, n); }

static void show(uint32_t node) {
    cm_ui_node n, root;
    char t[96];
    if (!g_on || !cm_ui_node_read(rdm, node, &n) || !cm_ui_node_read(rdm, cm_ui_root(rdm), &root)) {
        api->label_set("ui_inspect", 0, 0, "", 0, 0);
        return;
    }
    int len = snprintf(t, sizeof t, " %s %d,%d %dx%d ", n.cls, n.ax, n.ay, n.rw, n.rh);
    int x = n.ax, y = n.ay + n.rh;                    /* under the element, else above it */
    if (y >= root.h) y = n.ay - 1;
    if (y < 0) y = 0;
    if (x + len > root.w) x = root.w - len;
    if (x < 0) x = 0;
    api->label_set("ui_inspect", x, y, t, 0x000000, 0xffc040);
}

static int __cdecl on_ui(void *user, const cm_ui_event *e) {
    if (e->type == CM_UI_HOVER) show(e->node);
    return 0;
}

static void __cdecl s_inspect(cm_call *c) {
    g_on = api->arg_bool(c, 0);
    if (!g_on) api->label_set("ui_inspect", 0, 0, "", 0, 0);
}

__declspec(dllexport) int __cdecl cogmind_plugin_init(const cm_host_api *a) {
    api = a;
    if (a->version < CM_API_VERSION) { a->log("ui_inspect: needs a newer SDL.dll"); return 1; }
    cm_addr_init(a);                 /* game addresses as resolved for the running build */
    a->on_ui(on_ui, NULL);
    a->register_function("void ui_inspect(bool)", s_inspect);
    return 0;
}
