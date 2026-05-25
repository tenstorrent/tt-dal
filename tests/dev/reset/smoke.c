// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

#include <assert.h>
#include <stdlib.h>
#include <ttdal.h>

// MARK: hardware
int main(void) {
    tt_device_t dev;
    if (tt_dev_scan(1, &dev) <= 0)
        return EXIT_FAILURE;
    // Reset must succeed and leave the device in a usable state
    assert(tt_reset(&dev) == 0);
}
