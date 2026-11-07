// SPDX-License-Identifier: MPL-2.0
// Message logs: std::vector<LogMsg*>, oldest first.
#include "cogmind.h"
#include "base/mem.h"
#include "game/msglog.h"

int log_count(uint32_t log) {
    uint32_t v[2];
    if (!rd(log, v, 8) || v[1] < v[0] || (v[1] - v[0]) % 4) return 0;
    return (int)((v[1] - v[0]) / 4);
}
int log_message(uint32_t log, int i, char *out, size_t cap, int *turn) {
    if (i < 0 || i >= log_count(log)) return 0;
    uint32_t m = rd32(rd32(log) + 4 * (uint32_t)i);
    if (!m || !rd_string(m + LOGMSG_TEXT, out, cap)) return 0;
    if (turn) *turn = rdi(m + LOGMSG_TURN);
    return 1;
}
