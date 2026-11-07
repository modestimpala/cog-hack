// SPDX-License-Identifier: MPL-2.0
// Guarded access to game memory. Reads go through ReadProcessMemory, so a bad or stale pointer
// fails instead of crashing. All of this runs on the game's main thread.
#pragma once
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
int      rd(uint32_t addr, void *out, size_t n);
uint32_t rd32(uint32_t addr);
int32_t  rdi(uint32_t addr);
int      rd_string(uint32_t addr, char *out, size_t cap);   // MSVC10 std::string
// Writes are refused on non-writable pages (code, rdata) unless dev_mode=1, and on an unverified
// game build. 1 ok, 0 refused or failed.
int      wr(uint32_t addr, const void *src, size_t n);
int      wr32(uint32_t addr, uint32_t v);
// Do the bytes at addr match? Cached per call site (*cache starts at 0); game code doesn't change.
int      sig_ok(uint32_t addr, const uint8_t *bytes, size_t n, int *cache);
// Generational handle registries (see cogmind.h "handles")
uint32_t resolve(uint32_t registry_global, uint32_t handle);
uint32_t registry_count(uint32_t registry_global);
uint32_t registry_slot(uint32_t registry_global, uint32_t idx);
#ifdef __cplusplus
}
#endif
