// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <assert.h>
#include <errno.h>
#include <ttdal.h>

int main(void) {
    // Unopened session (fd = -1) must be rejected
    tt_session_t sess = { .dev = { .id = 0 }, .fd = -1 };
    tt_dev_info_t info;
    assert(tt_dev_info(&sess, &info) < 0);
    assert(errno == ENOTCONN);
}
