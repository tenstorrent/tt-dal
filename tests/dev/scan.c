// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

#include "common.h"
#include <assert.h>
#include <ttdal.h>

// MARK: hardware
int main(void) {
    tt_device_t devs[1];
    // Verify at least one device is discoverable and openable
    tt_device_t *dev = open_test_device(devs);
    if (!dev)
        return EXIT_FAILURE;
    tt_dev_close(dev);
}
