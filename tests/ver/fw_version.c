// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

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
    int rc = tt_fw_version(sess, &ver);
    // Firmware version should be at least 19.x
    if (rc == 0)
        assert(ver.major >= 19);
    assert(tt_close(sess) == 0);
}
