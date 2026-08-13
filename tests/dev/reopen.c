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

    // Reopen replaces the descriptor in place
    assert(tt_reopen(sess) == 0);
    assert(sess->fd >= 0);

    assert(tt_close(sess) == 0);
}
