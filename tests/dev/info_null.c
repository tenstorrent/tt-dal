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
    // NULL info pointer must be rejected even on an open device
    assert(tt_dev_info(dev, NULL) < 0);
    assert(tt_errno == TT_EINVAL);
    tt_dev_close(dev);
}
