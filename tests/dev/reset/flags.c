// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <assert.h>
#include <errno.h>
#include <ttdal.h>

int main(void) {
    // Unknown flag bits must be rejected before touching the device
    tt_device_t dev = { .id = 0 };
    assert(tt_reset(&dev, 1U << 15) < 0);
    assert(errno == EINVAL);

    // Unknown flag bits must be rejected before checking the session
    tt_session_t sess = { .dev = { .id = 0 }, .fd = -1 };
    assert(tt_reset_with(&sess, 1U << 15) < 0);
    assert(errno == EINVAL);
}
