// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

#pragma once

#include <assert.h>
#include <stdlib.h>
#include <ttdal.h>

/// Open the first available device.
///
/// Returns NULL if the driver is not installed or no devices are found. Asserts
/// on unexpected open failure.
static inline tt_session_t *open_test_device(tt_session_t *sess) {
    tt_device_t dev;
    if (tt_dev_scan(1, &dev) <= 0)
        return NULL;
    assert(tt_open(&dev, sess, 0) == 0);
    return sess;
}
