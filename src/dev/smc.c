// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

#include "err.h"
#include "ioctl.h"
#include "ttdal.h"

#include <stdlib.h>

/// Send SMC message to device.
///
/// Dispatches to architecture-specific implementation based on device
/// architecture.
int tt_message(
    const tt_session_t *sess, tt_message_t *msg, bool wait, uint32_t timeout
) {
    // Validate args
    if (!sess || !msg)
        return tt_fail(EINVAL);

    // Ensure session is open
    if (sess->fd < 0)
        return tt_fail(ENOTCONN);

    abort(); // unimplemented
}
