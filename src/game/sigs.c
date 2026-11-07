// SPDX-License-Identifier: MPL-2.0
// Signature files (sigs/cogmind.sigs; format in its header) and the scan that re-finds each
// address in a build of the game.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cogmind.h"
#include "base/log.h"
#include "game/sigs.h"

static const char BUILTIN[] =
#include "sigs_builtin.inc"   // build/: sigs/cogmind.sigs as a C string (Makefile)
;

#define MAX_SIGS    256
#define MAX_DERIVED 64
#define MAX_PAT     128
typedef struct { int id, is_fn, operand, n; uint8_t b[MAX_PAT], m[MAX_PAT]; } sig;
typedef struct { int id, base; uint32_t delta; } derived;
static sig g_sigs[MAX_SIGS];
static derived g_derived[MAX_DERIVED];
static int g_nsigs, g_nderived;

static int addr_id(const char *name) {
    for (int i = 0; i < CM_A_COUNT; ++i) if (!strcmp(cm_addr_names[i], name)) return i;
    return -1;
}

// "8B 0D ?? ?? ?? ??" -> bytes + mask; returns the length, -1 on a bad token
static int parse_pattern(const char *p, uint8_t *b, uint8_t *m, int cap) {
    int n = 0;
    for (;;) {
        while (*p == ' ' || *p == '\t') ++p;
        if (!*p || *p == '\r' || *p == '\n' || *p == '#') return n;
        if (n >= cap) return -1;
        if (p[0] == '?') { b[n] = 0; m[n] = 0; p += p[1] == '?' ? 2 : 1; }
        else {
            char *end;
            unsigned long v = strtoul(p, &end, 16);
            if (end != p + 2 || v > 0xff) return -1;
            b[n] = (uint8_t)v; m[n] = 1; p = end;
        }
        ++n;
    }
}

// one line; 1 ok (or ignorable), 0 bad
static int parse_line(char *line, uint32_t *stamp, uint32_t *image) {
    char name[64], kind[8], base[64];
    unsigned a, b;
    int off, used = 0;
    while (*line == ' ' || *line == '\t') ++line;
    if (!*line || *line == '#' || *line == '\r' || *line == '\n') return 1;
    if (sscanf(line, "build %x %x", &a, &b) == 2) { *stamp = a; *image = b; return 1; }
    if (sscanf(line, "%63s = %63s + %x", name, base, &a) == 3) {
        int id = addr_id(name), bid = addr_id(base);
        if (id < 0 || bid < 0) { hlog("sigs: unknown address in '%s', ignored", name); return 1; }
        if (g_nderived >= MAX_DERIVED) return 0;
        g_derived[g_nderived++] = (derived){ id, bid, a };
        return 1;
    }
    if (sscanf(line, "%63s %7s %d %n", name, kind, &off, &used) < 3 || !used) return 0;
    int is_fn = !strcmp(kind, "code");
    if (!is_fn && strcmp(kind, "data")) return 0;
    int id = addr_id(name);
    if (id < 0) { hlog("sigs: unknown address %s, ignored", name); return 1; }
    if (g_nsigs >= MAX_SIGS) return 0;
    sig *s = &g_sigs[g_nsigs];
    s->id = id; s->is_fn = is_fn; s->operand = off;
    s->n = parse_pattern(line + used, s->b, s->m, MAX_PAT);
    if (s->n <= 0 || off < 0 || off + (is_fn ? 0 : 4) > s->n) return 0;
    ++g_nsigs;
    return 1;
}

int sigs_parse(const char *text, uint32_t *stamp, uint32_t *image) {
    g_nsigs = g_nderived = 0;
    *stamp = *image = 0;
    for (int ln = 1; *text; ++ln) {
        const char *e = strchr(text, '\n');
        size_t n = e ? (size_t)(e - text) : strlen(text);
        char line[512];
        if (n >= sizeof line) n = sizeof line - 1;
        memcpy(line, text, n);
        line[n] = 0;
        if (!parse_line(line, stamp, image)) {
            hlog("sigs: line %d not understood: %s", ln, line);
            g_nsigs = g_nderived = 0;
            return -1;
        }
        text = e ? e + 1 : text + n;
    }
    return g_nsigs;
}

int sigs_load_builtin(uint32_t *stamp, uint32_t *image) { return sigs_parse(BUILTIN, stamp, image); }

int sigs_load_file(const char *path, uint32_t *stamp, uint32_t *image) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    static char buf[256 * 1024];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    return sigs_parse(buf, stamp, image);
}

// matches of a masked pattern in text (stops at 2); *at = offset of the first
static int scan(const uint8_t *text, size_t size, const uint8_t *b, const uint8_t *m, int n, size_t *at) {
    int first = -1;
    for (int i = 0; i < n; ++i) if (m[i]) { first = i; break; }
    if (first < 0 || size < (size_t)n) return 0;
    int hits = 0;
    for (size_t pos = 0; pos + (size_t)n <= size; ++pos) {
        const uint8_t *q = memchr(text + pos + first, b[first], size - pos - (size_t)n + 1);
        if (!q) break;
        pos = (size_t)(q - text) - (size_t)first;
        int ok = 1;
        for (int i = 0; i < n && ok; ++i) if (m[i] && text[pos + i] != b[i]) ok = 0;
        if (ok) { if (!hits++) *at = pos; if (hits >= 2) break; }
    }
    return hits;
}

int sigs_resolve(const uint8_t *text, size_t size, uint32_t text_va, int verbose, uint32_t *out, int *moved, int *lost) {
    int mv = 0, ls = 0;
    for (int id = 0; id < CM_A_COUNT; ++id) out[id] = 0;
    for (int id = 0; id < CM_A_COUNT; ++id) {
        const char *name = cm_addr_names[id];
        uint32_t found = 0;
        int sites = 0, agree = 1, has = 0;
        for (int j = 0; j < g_nsigs; ++j) {
            const sig *s = &g_sigs[j];
            if (s->id != id) continue;
            has = 1;
            size_t at = 0;
            if (scan(text, size, s->b, s->m, s->n, &at) != 1) continue;   // gone or ambiguous at this site
            uint32_t v;
            if (s->is_fn) v = text_va + (uint32_t)at + (uint32_t)s->operand;
            else memcpy(&v, text + at + s->operand, 4);
            if (sites++ && v != found) agree = 0;
            found = v;
        }
        if (!has) continue;                                                 // derived: below
        if (!sites || !agree) { ++ls; hlog("  %-22s %08x  %s", name, cm_addr_defaults[id], !sites ? "MISSING" : "CONFLICT (sites disagree)"); continue; }
        out[id] = found;
        if (found != cm_addr_defaults[id]) { ++mv; hlog("  %-22s %08x  MOVED -> %08x", name, cm_addr_defaults[id], found); }
        else if (verbose) hlog("  %-22s %08x  ok (%d site%s)", name, found, sites, sites > 1 ? "s" : "");
    }
    // addresses only reached as base + offset: the base's resolved value + delta
    for (int i = 0; i < g_nderived; ++i) {
        const derived *d = &g_derived[i];
        const char *name = cm_addr_names[d->id], *base = cm_addr_names[d->base];
        if (!out[d->base]) { ++ls; hlog("  %-22s %08x  MISSING (base %s)", name, cm_addr_defaults[d->id], base); continue; }
        out[d->id] = out[d->base] + d->delta;
        if (out[d->id] != cm_addr_defaults[d->id]) { ++mv; hlog("  %-22s %08x  MOVED -> %08x (= %s + %#x)", name, cm_addr_defaults[d->id], out[d->id], base, d->delta); }
        else if (verbose) hlog("  %-22s %08x  ok (%s + %#x)", name, out[d->id], base, d->delta);
    }
    if (moved) *moved = mv;
    if (lost) *lost = ls;
    return mv + ls;
}
