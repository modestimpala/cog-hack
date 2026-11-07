// SPDX-License-Identifier: MPL-2.0
// hack\config.txt: key=value settings, read once at startup.
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
void        config_load(const char *path);
int         cfg_int(const char *key, int def);
const char *cfg_str(const char *key, const char *def);   // raw value ("ctrl+f9"), def if absent
#ifdef __cplusplus
}
#endif
