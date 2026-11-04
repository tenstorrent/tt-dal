// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <ttdal.h>
#include <unistd.h>

int main(void) {
    // Absent device, descriptor closed behind the library's back
    int fds[2];
    assert(pipe(fds) == 0);
    assert(close(fds[1]) == 0);
    tt_session_t sess = { .dev = { .id = UINT32_MAX }, .fd = fds[0] };
    assert(close(fds[0]) == 0);

    // Failed reopen must leave the session closed
    assert(tt_reopen(&sess) < 0);
    assert(errno == ENODEV);
    assert(sess.fd == -1);
}
