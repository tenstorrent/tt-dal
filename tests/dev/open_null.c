// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <assert.h>
#include <errno.h>
#include <ttdal.h>

int main(void) {
    // NULL pointers must be rejected
    tt_device_t dev = { .id = 0 };
    tt_session_t sess;
    assert(tt_open(NULL, &sess, 0) < 0);
    assert(errno == EINVAL);
    assert(tt_open(&dev, NULL, 0) < 0);
    assert(errno == EINVAL);
}
