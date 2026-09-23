// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "common.h"
#include <assert.h>
#include <ttdal.h>

// MARK: hardware
int main(void) {
    tt_session_t s;
    tt_session_t *sess = open_test_device(&s);
    if (!sess)
        return EXIT_FAILURE;

    tt_telemetry_t table;
    assert(tt_telemetry(sess, table) == 0);
    assert(table[TT_TAG_TIMER_HEARTBEAT] != 0);
    assert(table[TT_TAG_AICLK] > 0);

    assert(tt_close(sess) == 0);
}
