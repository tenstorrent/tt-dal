// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

#include <assert.h>
#include <stdlib.h>
#include <ttdal.h>

// MARK: hardware
int main(void) {
    tt_device_t dev;
    if (tt_dev_scan(1, &dev) <= 0)
        return EXIT_FAILURE;

    // Reset must succeed without a session
    assert(tt_reset(&dev) == 0);

    // Reset via a session must succeed and consume the session
    tt_session_t sess;
    assert(tt_open(&dev, &sess) == 0);
    assert(tt_reset_with(&sess) == 0);
    assert(sess.fd == -1);

    // Device must be usable after reset
    assert(tt_open(&sess.dev, &sess) == 0);
    assert(tt_close(&sess) == 0);
}
