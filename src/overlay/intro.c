// SPDX-License-Identifier: MPL-2.0
// The new-game boot sequence (SEQUENCE INITIATED ... PREPARING CONTROL OVERLAY) gets one more line:
// after the game has revealed the `intro_after` line, `intro_text` is typed two rows below it, the
// same three-cell process cursor runs, then the `intro_tag` box appears. Font, colors and timing are
// the sequence's own, measured from a UI recording of it (tools/re/uirec.py):
//   text: one glyph per frame, the newest one bright (0,255,0 on 0,64,0), then 0,178,0 on black;
//   spaces stay transparent; the cursor cell fades (178 -> 135, 73 -> 49, 49 -> 25 over 0.5 s each);
//   status boxes are black on 0,217,0 (okay) or 178,0,0 (alert).
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "cogmind.h"
#include "cm_ui.h"
#include "base/config.h"
#include "base/log.h"
#include "base/mem.h"
#include "hook/sdl.h"
#include "overlay/backing.h"
#include "overlay/font.h"
#include "overlay/intro.h"

enum { IDLE, WAIT, REVEAL, PROCESS, TAG, FADE, DONE };
static int g_state;
static uint32_t g_intro, g_played;   // the CIntro we are drawing in; the one we already played in
static DWORD g_t;                    // when the state began
static int g_ax, g_ay, g_font, g_n;  // root cell of our line, its font, glyphs revealed
static char g_text[96], g_tag[48];

static int trimmed(const char *key, const char *def, char *out, size_t cap) {
    const char *v = cfg_str(key, def);
    while (*v == ' ') ++v;
    snprintf(out, cap, "%s", v);
    size_t n = strlen(out);
    while (n && out[n - 1] == ' ') out[--n] = 0;
    return n > 0;
}

// a visible direct child of XRoot by class (the title and the boot sequence are top-level windows)
static uint32_t root_child(const char *cls) {
    cm_ui_node r;
    uint32_t root = cm_ui_root(rd);
    if (!root || !cm_ui_node_read(rd, root, &r)) return 0;
    for (uint32_t i = 0; i < r.nkids; ++i) {
        uint32_t a = cm_ui_child(rd, &r, i);
        cm_ui_node n;
        if (cm_ui_node_read(rd, a, &n) && !n.hidden && !strcmp(n.cls, cls)) return a;
    }
    return 0;
}

// the `intro_after` line, if it is on screen and fully revealed (every glyph cell has an opaque background)
static int trigger_line(uint32_t intro, cm_ui_node *out) {
    char want[96], text[256];
    if (!trimmed("intro_after", "PREPARING CONTROL OVERLAY", want, sizeof want)) return 0;
    cm_ui_node in;
    if (!cm_ui_node_read(rd, intro, &in)) return 0;
    for (uint32_t i = 0; i < in.nkids; ++i) {
        cm_ui_node n;
        if (!cm_ui_node_read(rd, cm_ui_child(rd, &in, i), &n) || n.hidden || n.h != 1 || n.w < 3 || n.w > 200) continue;
        if (strcmp(n.cls, "CText") || !cm_ui_row_text(rd, &n, 0, text, sizeof text)) continue;
        size_t len = strlen(text);
        while (len && text[len - 1] == ' ') text[--len] = 0;
        if (strcmp(text, want)) continue;
        for (int x = 0; x < n.w; ++x) {
            cm_cell c;
            if (!cm_ui_cell(rd, &n, x, 0, &c)) return 0;
            if (c.ch != ' ' && c.ch && CELL_IS_TRANSPARENT(c.bg)) return 0;   // still typing
        }
        *out = n;
        return 1;
    }
    return 0;
}

static void cell(SDL_Surface *s, int x, int y, const game_font *gf, int glyph, uint32_t fg, uint32_t bg) {
    SDL_Rect r = { (int16_t)x, (int16_t)y, (uint16_t)gf->gw, (uint16_t)gf->gh };
    real_FillRect(s, &r, real_MapRGB(s->format, bg >> 16 & 0xff, bg >> 8 & 0xff, bg & 0xff));
    if (glyph) font_blit_glyph(s, x, y, gf, glyph, fg);
}

int intro_draw(SDL_Surface *s) {
    if (!cfg_int("intro_line", 1) || !s || !s->format || s->format->BytesPerPixel != 4 || !s->pixels) return 0;
    uint32_t intro = root_child("CIntro");
    if (!intro) { g_state = IDLE; g_intro = 0; return 0; }
    DWORD now = GetTickCount();
    if (g_state == IDLE) {
        cm_ui_node line;
        if (intro == g_played || !trigger_line(intro, &line)) return 0;
        if (!trimmed("intro_text", "INJECTING COG-HACK", g_text, sizeof g_text)) return 0;
        char tag[40];
        if (trimmed("intro_tag", "ACTIVE", tag, sizeof tag)) snprintf(g_tag, sizeof g_tag, " %s ", tag);
        else g_tag[0] = 0;
        g_intro = g_played = intro;
        g_ax = line.ax; g_ay = line.ay + 2; g_font = line.font;
        g_state = WAIT; g_t = now; g_n = 0;
        hlog("intro line: \"%s\" at %d,%d after \"%s\"", g_text, g_ax, g_ay, cfg_str("intro_after", ""));
        return 0;
    }
    if (intro != g_intro) { g_state = IDLE; return 0; }
    if (g_state == DONE) return 0;
    cm_ui_node line;
    if (g_state != FADE && !trigger_line(intro, &line)) { g_state = FADE; g_t = now; }   // the game dissolves its lines: so do we
    game_font gf;
    int rgw, rgh;
    if (!font_load(g_font, &gf) || !cm_ui_font_size(rd, 0, &rgw, &rgh)) return 0;
    int len = (int)strlen(g_text), taglen = (int)strlen(g_tag);
    DWORD el = now - g_t;
    switch (g_state) {
    case WAIT:    if (el >= 300) { g_state = REVEAL; g_t = now; } return 0;
    case REVEAL:  g_n = (int)(el / 16) + 1; if (g_n >= len) { g_n = len; g_state = PROCESS; g_t = now; } break;
    case PROCESS: if (el >= (DWORD)cfg_int("intro_process_ms", 600)) { g_state = TAG; g_t = now; } break;
    case FADE:    if (el >= 300) { g_state = DONE; return 0; } break;
    default: break;
    }
    int x0 = rdi(ADDR_DISPLAY + DISPLAY_ORIGIN_X) + g_ax * rgw, y = rdi(ADDR_DISPLAY + DISPLAY_ORIGIN_Y) + g_ay * rgh;
    int cursor = x0 + (len + 1) * gf.gw, extra = taglen > 3 ? taglen : 3;
    backing_save(s, x0, y, (len + 1 + extra) * gf.gw, gf.gh);
    if (g_state == FADE) {                                            // thinning dim blocks, like the game's block fade
        uint32_t frame = now / 33;
        for (int i = 0; i < len + 1 + taglen; ++i) {
            if (i < len ? g_text[i] == ' ' : i == len) continue;
            uint32_t h = (uint32_t)i * 0x9E3779B1u ^ frame * 0x85EBCA77u; h ^= h >> 13; h *= 0x2C1B3C6Du; h ^= h >> 11;
            if ((h & 0xff) < (300 - el) * 255 / 300) cell(s, x0 + i * gf.gw, y, &gf, 0, 0, 0x002000);
        }
        return 1;
    }
    for (int i = 0; i < g_n; ++i) {
        unsigned char c = (unsigned char)g_text[i];
        if (c == ' ') continue;                                   // the rain shows through spaces
        int newest = g_state == REVEAL && i == g_n - 1;
        int gi = c >= 'A' && c <= 'Z' ? 64 + c - 'A' : c >= 'a' && c <= 'z' ? 96 + c - 'a' : c >= 32 && c <= 63 ? c - 32 : 0;
        cell(s, x0 + i * gf.gw, y, &gf, gi, newest ? 0x00ff00 : 0x00b200, newest ? 0x004000 : 0x000000);
    }
    if (g_state == PROCESS) {
        static const int base[3] = { 178, 73, 49 }, drop[3] = { 43, 24, 24 };
        int phase = (int)(el / 500) % 3, v = base[phase] - (int)((el % 500) * (DWORD)drop[phase] / 500);
        cell(s, cursor + phase * gf.gw, y, &gf, 0, 0, (uint32_t)v << 8);
    } else if (g_state == TAG) {
        for (int i = 0; i < taglen; ++i) {
            unsigned char c = (unsigned char)g_tag[i];
            int gi = c >= 'A' && c <= 'Z' ? 64 + c - 'A' : c >= 'a' && c <= 'z' ? 96 + c - 'a' : c >= 33 && c <= 63 ? c - 32 : 0;
            cell(s, cursor + i * gf.gw, y, &gf, gi, 0x000000, cfg_int("intro_alert", 0) ? 0xb20000 : 0x00d900);
        }
    }
    return 1;
}
