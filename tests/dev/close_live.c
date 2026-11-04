// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <ttdal.h>
#include <unistd.h>

int main(void) {
    // Session around a live descriptor
    int fds[2];
    assert(pipe(fds) == 0);
    tt_session_t sess = { .dev = { .id = 0 }, .fd = fds[0] };

    // Close must release the descriptor and invalidate the handle
    assert(tt_close(&sess) == 0);
    assert(sess.fd == -1);
    assert(fcntl(fds[0], F_GETFD) < 0);
    assert(errno == EBADF);

    // Second close must be a no-op
    assert(tt_close(&sess) == 0);

    assert(close(fds[1]) == 0);
}
