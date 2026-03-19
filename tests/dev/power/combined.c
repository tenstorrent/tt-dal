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
    // Request all four defined power flags together
    int rc = tt_power(dev, 0x000F);
    if (rc < 0)
        printf("combined (0x000F) failed: tt_errno=%d\n", tt_errno);
    tt_dev_close(dev);
}
