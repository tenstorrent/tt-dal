// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

#include "common.h"
#include <assert.h>
#include <stdio.h>
#include <ttdal.h>

// MARK: hardware
int main(void) {
    tt_device_t devs[1];
    tt_device_t *dev = open_test_device(devs);
    if (!dev)
        return EXIT_FAILURE;
    // Enable L2CPU
    int rc = tt_power(dev, TT_POWER_L2CPU_ENABLE);
    if (rc < 0)
        printf("l2cpu_enable failed: tt_errno=%d\n", tt_errno);
    tt_dev_close(dev);
}
