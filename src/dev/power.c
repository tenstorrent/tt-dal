// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

#include "ioctl.h"
#include "ttdal.h"

#include <sys/ioctl.h>

/// Set device power state.
///
/// Issues the `SET_POWER_STATE` `ioctl` with all four defined flag bits
/// marked valid. Any flag not set in `flags` is explicitly turned off.
int tt_power(const tt_device_t *dev, uint16_t flags) {
    // Validate args
    if (!dev)
        return tt_errno = TT_EINVAL, TT_ERR;

    // Ensure open
    if (dev->fd < 0)
        return tt_errno = TT_ENOTOPEN, TT_ERR;

    // Build `ioctl` struct.
    //
    // Mark all four defined flag bits valid so the driver treats unset bits
    // as explicit off requests, not "don't care".
    struct tenstorrent_power_state power = {
        .argsz       = sizeof(power),
        .flags       = 0,
        .reserved0   = 0,
        .validity    = TT_POWER_VALIDITY(4, 0),
        .power_flags = flags,
    };

    // Issue `ioctl`
    if (ioctl(dev->fd, TENSTORRENT_IOCTL_SET_POWER_STATE, &power) != 0)
        return tt_errno = TT_EIO, TT_ERR;

    return TT_OK;
}
