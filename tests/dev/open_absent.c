// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <ttdal.h>

int main(void) {
    // Pick an absent device.
    //
    // `tt-kmd` names device nodes with 31-bit ordinals, so the largest
    // `uint32_t` never names a device.
    tt_device_t dev = { .id = UINT32_MAX };

    // Opening a device that does not exist must report `ENODEV`
    tt_session_t sess;
    assert(tt_open(&dev, &sess, 0) < 0);
    assert(errno == ENODEV);
}
