// SPDX-License-Identifier: MPL-2.0
// The game's message logs (ADDR_MSG_LOG / ADDR_COMBAT_LOG).
#pragma once
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
int log_count(uint32_t log);
int log_message(uint32_t log, int i, char *out, size_t cap, int *turn);   // i = 0 is the oldest; 1 ok
#ifdef __cplusplus
}
#endif
