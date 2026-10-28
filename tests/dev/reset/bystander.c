// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <ttdal.h>

// MARK: hardware
int main(void) {
    tt_device_t dev;
    if (tt_dev_scan(1, &dev) <= 0)
        return EXIT_FAILURE;

    // Open two sessions on the same device
    tt_session_t self, other;
    assert(tt_open(&dev, &self, 0) == 0);
    assert(tt_open(&dev, &other, 0) == 0);

    // A shared session must not reset under the bystander
    assert(tt_reset_with(&self) < 0);
    assert(errno == EPERM);

    // The bystander is untouched
    tt_dev_info_t info;
    assert(tt_dev_info(&other, &info) == 0);
    assert(tt_close(&other) == 0);
    assert(tt_close(&self) == 0);

    // An exclusive session resets once the bystander is gone
    assert(tt_open(&dev, &self, TT_OPEN_EXCL) == 0);
    assert(tt_reset_with(&self) == 0);

    // The resetting session survives
    tt_telemetry_t table;
    assert(tt_telemetry(&self, table) == 0);
    assert(tt_close(&self) == 0);
}
