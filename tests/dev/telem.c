// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "common.h"
#include <assert.h>
#include <ttdal.h>

/// Firmware heartbeat tag.
#define TELEM_HEARTBEAT 32
/// Firmware AI clock tag.
#define TELEM_AICLK 14

/// Find the entry for `tag`.
static const tt_telemetry_t *
find(size_t len, const tt_telemetry_t *buf, uint16_t tag) {
    for (size_t i = 0; i < len; i++) {
        if (buf[i].tag == tag)
            return &buf[i];
    }
    return NULL;
}

// MARK: hardware
int main(void) {
    tt_session_t s;
    tt_session_t *sess = open_test_device(&s);
    if (!sess)
        return EXIT_FAILURE;

    tt_telemetry_t table[256];
    ssize_t len = tt_telemetry(sess, 256, table);
    assert(len > 0);
    size_t n = (len < 256) ? (size_t)len : 256;

    // Tags are unique
    for (size_t i = 0; i < n; i++) {
        assert(table[i].tag != 0);
        assert(find(i, table, table[i].tag) == NULL);
    }

    const tt_telemetry_t *beat = find(n, table, TELEM_HEARTBEAT);
    assert(beat && beat->raw != 0);
    const tt_telemetry_t *aiclk = find(n, table, TELEM_AICLK);
    assert(aiclk && aiclk->raw > 0);

    // A short buffer reports the full count
    tt_telemetry_t one[1];
    assert(tt_telemetry(sess, 1, one) == len);
    assert(one[0].tag == table[0].tag);

    assert(tt_close(sess) == 0);
}
