// SPDX-License-Identifier: MPL-2.0
// test_sigs.exe <COGMIND.exe> [file.sigs]: the startup signature check (src/game/sigs.c) run on the
// game exe from disk, with the built-in signatures or, if given, a sidecar file. Every address must
// resolve to its cogmind.h value: catches stale signatures
// (regenerate: python3 tools/re/gen_sigs.py COGMIND.exe include/cogmind.h > sigs/cogmind.sigs).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "cogmind.h"
#include "base/log.h"
#include "game/sigs.h"

int main(int argc, char **argv) {
    if (argc < 2) { puts("usage: test_sigs <COGMIND.exe> [file.sigs]"); return 2; }
    uint32_t stamp, image;
    int nsigs = argc > 2 ? sigs_load_file(argv[2], &stamp, &image) : sigs_load_builtin(&stamp, &image);
    if (nsigs <= 0) { printf("signatures: %s did not load\n", argc > 2 ? argv[2] : "built-in set"); return 1; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { printf("signatures: skipped (no %s)\n", argv[1]); return 0; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)n);
    if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) return 2;
    fclose(f);
    log_open("test_sigs.log");
    uint32_t pe = *(uint32_t *)(b + 0x3c), base = *(uint32_t *)(b + pe + 24 + 28);
    uint16_t ns = *(uint16_t *)(b + pe + 6), opt = *(uint16_t *)(b + pe + 20);
    for (int i = 0; i < ns; ++i) {
        uint8_t *s = b + pe + 24 + opt + i * 40;
        if (memcmp(s, ".text", 6)) continue;
        uint32_t vs = *(uint32_t *)(s + 8), va = *(uint32_t *)(s + 12), rs = *(uint32_t *)(s + 16), ro = *(uint32_t *)(s + 20);
        static uint32_t found[CM_A_COUNT];
        int moved, lost;
        int bad = sigs_resolve(b + ro, vs < rs ? vs : rs, base + va, 1, found, &moved, &lost);
        log_close();
        FILE *l = fopen("test_sigs.log", "r");
        char line[256];
        while (l && fgets(line, sizeof line, l)) if (strstr(line, "MISSING") || strstr(line, "MOVED") || strstr(line, "CONFLICT")) printf("  %s", line);
        if (l) fclose(l);
        printf("signatures (%s, %d): %s (%d address%s off)\n", argc > 2 ? argv[2] : "built-in", nsigs,
               bad ? "FAIL" : "all match COGMIND.exe", bad, bad == 1 ? "" : "es");
        if (bad) return 1;
        /* simulated update: the World pointer moves 4 bytes -> it must be reported MOVED, with the new address */
        uint8_t *t = b + ro;
        size_t tn = vs < rs ? vs : rs;
        int patched = 0;
        for (size_t k = 0; k + 4 <= tn; ++k)
            if (*(uint32_t *)(t + k) == 0x00cf0d28u) { *(uint32_t *)(t + k) = 0x00cf0d2cu; ++patched; }
        log_open("test_sigs_moved.log");
        int bad2 = sigs_resolve(t, tn, base + va, 0, found, &moved, &lost);
        log_close();
        int saw = 0;
        l = fopen("test_sigs_moved.log", "r");
        while (l && fgets(line, sizeof line, l)) if (strstr(line, "ADDR_WORLD_PTR") && strstr(line, "MOVED -> 00cf0d2c")) saw = 1;
        if (l) fclose(l);
        int ok = bad2 == 1 && moved == 1 && lost == 0 && saw && found[CM_A_ADDR_WORLD_PTR] == 0x00cf0d2cu;
        printf("simulated update (World pointer +4, %d code sites): %s\n", patched, ok ? "detected, new address resolved" : "FAIL");
        return ok ? 0 : 1;
    }
    puts("signatures: no .text");
    return 1;
}
