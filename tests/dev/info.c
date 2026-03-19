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
    tt_dev_info_t info;
    assert(tt_dev_info(dev, &info) == 0);
    // Tenstorrent PCI vendor ID is 0x1e52
    assert(info.vendor_id != 0);
    tt_dev_close(dev);
}
