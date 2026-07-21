// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

#include <assert.h>
#include <stdlib.h>
#include <ttdal.h>

// MARK: hardware
int main(void) {
    tt_device_t dev;
    if (tt_dev_scan(1, &dev) <= 0)
        return EXIT_FAILURE;

    // Exclusive open must succeed on an idle device
    tt_session_t sess;
    assert(tt_open(&dev, &sess, TT_OPEN_EXCL) == 0);
    assert(tt_close(&sess) == 0);

    // Shared open must succeed once exclusivity is released
    assert(tt_open(&dev, &sess, 0) == 0);
    assert(tt_close(&sess) == 0);
}
