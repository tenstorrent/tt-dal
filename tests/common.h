// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

#pragma once

#include <assert.h>
#include <stdlib.h>
#include <ttdal.h>

/// Open the first available device.
///
/// Returns NULL if the driver is not installed or no devices are found. Asserts
/// on unexpected open failure.
static inline tt_device_t *open_test_device(tt_device_t devs[1]) {
    if (tt_dev_scan(1, devs) <= 0)
        return NULL;
    assert(tt_dev_open(&devs[0]) == 0);
    return &devs[0];
}
