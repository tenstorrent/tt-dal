// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

#include "common.h"
#include <assert.h>
#include <ttdal.h>

// MARK: hardware
int main(void) {
    tt_device_t devs[1];
    tt_device_t *dev = open_test_device(devs);
    if (!dev)
        return EXIT_FAILURE;
    tt_version_t ver;
    int rc = tt_version_firmware(dev, &ver);
    // Firmware version should be at least 19.x
    if (rc == 0)
        assert(ver.major >= 19);
    tt_dev_close(dev);
}
