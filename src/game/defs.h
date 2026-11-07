// SPDX-License-Identifier: MPL-2.0
// Item and robot definition tables, and their named numeric fields.
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
uint32_t    item_def_count(void);
uint32_t    item_def(uint32_t idx);
int         item_def_find(const char *name);     // index or -1 (cached)
int         item_def_index(uint32_t def_ptr);
uint32_t    bot_def_count(void);
uint32_t    bot_def(uint32_t idx);
int         bot_def_find(const char *name);
int         item_field_offset(const char *field);   // "mass" -> offset, or -1
int         bot_field_offset(const char *field);
const char *item_field_names(void);
const char *bot_field_names(void);
#ifdef __cplusplus
}
#endif
