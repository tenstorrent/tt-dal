// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <assert.h>
#include <errno.h>
#include <ttdal.h>
#include <unistd.h>

int main(void) {
    // Session around a descriptor closed behind the library's back
    int fds[2];
    assert(pipe(fds) == 0);
    assert(close(fds[1]) == 0);
    tt_session_t sess = { .dev = { .id = 0 }, .fd = fds[0] };
    assert(close(fds[0]) == 0);

    // Failed close must report the error and still invalidate the handle
    assert(tt_close(&sess) < 0);
    assert(errno == EBADF);
    assert(sess.fd == -1);

    // Second close must leave a file that reused the number alone
    int fd = dup(STDERR_FILENO);
    assert(fd == fds[0]);
    assert(tt_close(&sess) == 0);
    assert(close(fd) == 0);
}
