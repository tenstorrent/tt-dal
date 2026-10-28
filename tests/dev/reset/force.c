// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <ttdal.h>

// MARK: hardware
int main(void) {
    tt_device_t dev;
    if (tt_dev_scan(1, &dev) <= 0)
        return EXIT_FAILURE;

    // A forced reset must succeed while another session holds the device
    tt_session_t sess;
    assert(tt_open(&dev, &sess, 0) == 0);
    assert(tt_reset(&dev, TT_RESET_FORCE) == 0);

    // The holder's session must be invalidated
    tt_dev_info_t info;
    assert(tt_dev_info(&sess, &info) < 0);
    assert(errno == ECONNRESET);
    (void)tt_close(&sess);

    // A forced reset must still be refused under an exclusive holder
    assert(tt_open(&dev, &sess, TT_OPEN_EXCL) == 0);
    assert(tt_reset(&dev, TT_RESET_FORCE) < 0);
    assert(errno == EAGAIN);
    assert(tt_close(&sess) == 0);
}
