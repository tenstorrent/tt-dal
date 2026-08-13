// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "err.h"
#include "ioctl.h"
#include "ttdal.h"

#include <errno.h>
#include <sys/ioctl.h>

/// Set device power state.
///
/// Issues the `SET_POWER_STATE` `ioctl` with all four defined flag bits
/// marked valid. Any flag not set in `flags` is explicitly turned off.
int tt_power(const tt_session_t *sess, uint16_t flags) {
    // Validate args
    if (!sess)
        return tt_fail(EINVAL);

    // Ensure open
    if (sess->fd < 0)
        return tt_fail(ENOTCONN);

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
    if (ioctl(sess->fd, TENSTORRENT_IOCTL_SET_POWER_STATE, &power) != 0)
        return tt_fail_io(errno);

    return TT_OK;
}
