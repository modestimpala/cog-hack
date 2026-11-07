// SPDX-License-Identifier: MPL-2.0
// Byte signatures for the addresses in cogmind.h. The built-in set is sigs/cogmind.sigs, compiled
// in; a hack\sigs.txt in the same format replaces it (an updated set without a rebuild).
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "cogmind.h"
#ifdef __cplusplus
extern "C" {
#endif
// Parse a signature file. Returns the number of signatures, or -1 (and logs the line) on a bad file.
// *stamp / *image: the PE timestamp and image size of the build it was generated from (0 if absent).
int sigs_parse(const char *text, uint32_t *stamp, uint32_t *image);
int sigs_load_file(const char *path, uint32_t *stamp, uint32_t *image);   // -1 if missing or bad
int sigs_load_builtin(uint32_t *stamp, uint32_t *image);
// Resolve every address against a .text image into out[CM_A_COUNT] (0 = not found), with the
// signatures loaded last. *moved / *lost: found elsewhere / not found or conflicting. Returns moved + lost.
int sigs_resolve(const uint8_t *text, size_t size, uint32_t text_va, int verbose, uint32_t *out, int *moved, int *lost);
#ifdef __cplusplus
}
#endif
