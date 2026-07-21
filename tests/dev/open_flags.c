// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

#include <assert.h>
#include <errno.h>
#include <ttdal.h>

int main(void) {
    // Unknown flag bits must be rejected before touching the device
    tt_device_t dev = { .id = 0 };
    tt_session_t sess;
    assert(tt_open(&dev, &sess, 1U << 15) < 0);
    assert(errno == EINVAL);
}
