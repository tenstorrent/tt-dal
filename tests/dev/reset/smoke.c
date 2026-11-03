// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <assert.h>
#include <stdlib.h>
#include <ttdal.h>

// MARK: hardware
int main(void) {
    tt_device_t dev;
    if (tt_dev_scan(1, &dev) <= 0)
        return EXIT_FAILURE;

    // Reset must succeed without a session
    assert(tt_reset(&dev, 0) == 0);

    // Reset via a session must succeed and leave the session open
    tt_session_t sess;
    assert(tt_open(&dev, &sess, TT_OPEN_EXCL) == 0);
    assert(tt_reset_with(&sess, 0) == 0);

    // Session must still be usable after reset.
    //
    // Telemetry needs the device reinitialized, which info queries do not.
    tt_telemetry_t table[256];
    assert(tt_telemetry(&sess, 256, table) >= 0);
    assert(tt_close(&sess) == 0);
}
