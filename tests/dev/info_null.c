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
    // NULL info pointer must be rejected even on an open session
    assert(tt_dev_info(sess, NULL) < 0);
    assert(tt_errno == TT_EINVAL);
    tt_close(sess);
}
