// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

#include <assert.h>
#include <errno.h>
#include <ttdal.h>

int main(void) {
    // NULL device pointer must be rejected
    assert(tt_reset(NULL) < 0);
    assert(errno == EINVAL);

    // NULL session pointer must be rejected
    assert(tt_reset_with(NULL) < 0);
    assert(errno == EINVAL);

    // Closed session must be rejected
    tt_session_t sess = { .dev = { .id = 0 }, .fd = -1 };
    assert(tt_reset_with(&sess) < 0);
    assert(errno == ENOTCONN);
}
