// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <ttdal.h>

// MARK: hardware
int main(void) {
    tt_device_t dev;
    if (tt_dev_scan(1, &dev) <= 0)
        return EXIT_FAILURE;

    // Reset must be refused while another session holds the device
    tt_session_t sess;
    assert(tt_open(&dev, &sess) == 0);
    assert(tt_reset(&dev) < 0);
    assert(errno == EAGAIN);
    assert(tt_close(&sess) == 0);

    // Reset must succeed once the device is released
    assert(tt_reset(&dev) == 0);
}
