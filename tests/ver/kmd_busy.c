// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <assert.h>
#include <stdlib.h>
#include <ttdal.h>
#include <unistd.h>

// MARK: hardware
int main(void) {
    tt_device_t devs[64];
    ssize_t found = tt_dev_scan(64, devs);
    if (found <= 0)
        return EXIT_FAILURE;
    size_t count = (size_t)found < 64 ? (size_t)found : 64;

    // Hold every device exclusively
    tt_session_t excl[64];
    for (size_t i = 0; i < count; i++)
        assert(
            tt_open(&devs[i], &excl[i], TT_OPEN_EXCL | TT_OPEN_NONBLOCK) == 0
        );

    // Query must succeed without opening a device
    alarm(5); // a query that waits ends here instead
    tt_version_t ver;
    assert(tt_kmd_version(&ver) == 0);
    assert(ver.major >= 2);
    alarm(0);

    for (size_t i = 0; i < count; i++)
        assert(tt_close(&excl[i]) == 0);
}
