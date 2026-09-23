// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "common.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <ttdal.h>

// MARK: hardware
int main(void) {
    tt_session_t s;
    tt_session_t *sess = open_test_device(&s);
    if (!sess)
        return EXIT_FAILURE;
    // Enable L2CPU
    int rc = tt_power(sess, TT_POWER_L2CPU_ENABLE);
    if (rc < 0)
        printf("l2cpu_enable failed: errno=%d\n", errno);
    assert(tt_close(sess) == 0);
}
