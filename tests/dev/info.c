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
    tt_dev_info_t info;
    assert(tt_dev_info(sess, &info) == 0);
    // Tenstorrent PCI vendor ID is 0x1e52
    assert(info.vendor_id != 0);
    assert(tt_close(sess) == 0);
}
