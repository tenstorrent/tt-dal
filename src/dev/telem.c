// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

#include "ioctl.h"
#include "ttdal.h"

#include <stdlib.h>

/// Get device telemetry.
///
/// Dispatches to architecture-specific implementation based on device
/// architecture.
int tt_telemetry(const tt_device_t *dev, tt_telemetry_t table) {
    // Validate args
    if (!dev || !table)
        return tt_errno = TT_EINVAL, TT_ERR;

    // Ensure device is open
    if (dev->fd < 0)
        return tt_errno = TT_ENOTOPEN, TT_ERR;

    abort(); // unimplemented
}
