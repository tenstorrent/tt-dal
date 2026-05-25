// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

#include "common.h"
#include <assert.h>
#include <ttdal.h>

// MARK: hardware
int main(void) {
    tt_session_t s;
    tt_session_t *sess = open_test_device(&s);
    if (!sess)
        return EXIT_FAILURE;
    tt_version_t ver;
    int rc = tt_version_firmware(sess, &ver);
    // Firmware version should be at least 19.x
    if (rc == 0)
        assert(ver.major >= 19);
    tt_close(sess);
}
