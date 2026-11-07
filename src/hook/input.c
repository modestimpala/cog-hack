// SPDX-License-Identifier: MPL-2.0
// Synthetic key events, and key names ("ctrl+shift+f11"). Scripts and plugins queue keys on the
// main thread; the SDL_PollEvent hook hands them to the game one per poll, ahead of real input,
// exactly as if typed.
#include <stdlib.h>
#include <string.h>
#include "hook/sdl.h"
#include "hook/input.h"

#define QLEN 64
static uint8_t g_q[QLEN][SDL_EVENT_SIZE];
static int g_head, g_tail;

static void push(uint8_t type, int sym, int mod) {
    int next = (g_tail + 1) % QLEN;
    if (next == g_head) return;                     // full: drop
    uint8_t *e = g_q[g_tail];
    memset(e, 0, SDL_EVENT_SIZE);
    e[0] = type;
    e[2] = type == SDL_KEYDOWN;                     // state: pressed / released
    *(int32_t *)(e + 8) = sym;
    *(int32_t *)(e + 12) = mod;
    *(uint16_t *)(e + 16) = (sym > 0 && sym < 128) ? (uint16_t)sym : 0;
    g_tail = next;
}

void inject_key(int sym, int mod) {
    push(SDL_KEYDOWN, sym, mod);
    push(SDL_KEYUP, sym, mod);
}

int input_pop(void *ev) {
    if (!ev || g_head == g_tail) return 0;
    memcpy(ev, g_q[g_head], SDL_EVENT_SIZE);       // never more than sizeof(SDL_Event)
    g_head = (g_head + 1) % QLEN;
    return 1;
}

// "ctrl+shift+f11" -> SDL sym + modifier set (HK_CTRL | HK_SHIFT | HK_ALT). 1 ok, 0 unknown key.
int key_parse(const char *spec, int *sym, int *mods) {
    static const struct { const char *n; int sym; } named[] = {
        {"enter",13},{"return",13},{"esc",27},{"escape",27},{"space",32},{"tab",9},{"backspace",8},
        {"up",273},{"down",274},{"right",275},{"left",276},{"insert",277},{"home",278},{"end",279},
        {"pageup",280},{"pagedown",281},{"delete",127},{"pause",19},{"scrolllock",302},{"printscreen",316},
        {"kp0",256},{"kp1",257},{"kp2",258},{"kp3",259},{"kp4",260},{"kp5",261},{"kp6",262},{"kp7",263},{"kp8",264},{"kp9",265},
    };
    char k[32];
    int m = 0;
    for (;;) {
        while (*spec == ' ') ++spec;
        const char *plus = strchr(spec, '+'), *comma = strchr(spec, ',');
        if (!plus || !plus[1] || (comma && comma < plus)) break;   // a '+' after the comma is the next key's
        size_t n = (size_t)(plus - spec);
        if (n == 4 && !_strnicmp(spec, "ctrl", 4)) m |= HK_CTRL;
        else if (n == 5 && !_strnicmp(spec, "shift", 5)) m |= HK_SHIFT;
        else if (n == 3 && !_strnicmp(spec, "alt", 3)) m |= HK_ALT;
        else return 0;
        spec = plus + 1;
    }
    size_t n = 0;
    while (spec[n] && spec[n] != ' ' && spec[n] != ',' && n < sizeof k - 1) { k[n] = (char)(spec[n] | 0x20); ++n; }
    k[n] = 0;
    *mods = m;
    if (n == 1 && ((k[0] >= 'a' && k[0] <= 'z') || (k[0] >= '0' && k[0] <= '9'))) { *sym = k[0]; return 1; }
    for (size_t i = 0; i < sizeof named / sizeof *named; ++i) if (!strcmp(k, named[i].n)) { *sym = named[i].sym; return 1; }
    if (k[0] == 'f' && n >= 2 && n <= 3) { int f = atoi(k + 1); if (f >= 1 && f <= 15) { *sym = 281 + f; return 1; } }
    return 0;
}
