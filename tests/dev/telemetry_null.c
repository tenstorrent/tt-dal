// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

#include <assert.h>
#include <ttdal.h>

int main(void) {
    tt_telemetry_t table;
    // NULL session pointer must be rejected
    assert(tt_telemetry(NULL, table) < 0);
    assert(tt_errno == TT_EINVAL);

    // NULL table pointer must also be rejected
    tt_session_t sess = { .dev = { .id = 0 }, .fd = -1 };
    assert(tt_telemetry(&sess, NULL) < 0);
    assert(tt_errno == TT_EINVAL);
}
