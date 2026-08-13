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

    // Hold a shared session
    tt_session_t sess;
    assert(tt_open(&dev, &sess, 0) == 0);

    // Exclusive acquisition must fail fast while the device is held
    tt_session_t excl;
    assert(tt_open(&dev, &excl, TT_OPEN_EXCL | TT_OPEN_NONBLOCK) < 0);
    assert(errno == EAGAIN);

    // Exclusive acquisition must succeed once the device is released
    assert(tt_close(&sess) == 0);
    assert(tt_open(&dev, &excl, TT_OPEN_EXCL | TT_OPEN_NONBLOCK) == 0);

    // Shared non-blocking open must fail fast under an exclusive holder
    assert(tt_open(&dev, &sess, TT_OPEN_NONBLOCK) < 0);
    assert(errno == EAGAIN);

    assert(tt_close(&excl) == 0);
}
