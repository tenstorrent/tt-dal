// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

#include "common.h"
#include <assert.h>
#include <stdio.h>
#include <ttdal.h>

// MARK: hardware
int main(void) {
    tt_session_t s;
    tt_session_t *sess = open_test_device(&s);
    if (!sess)
        return EXIT_FAILURE;
    // Enable Tensix cores
    int rc = tt_power(sess, TT_POWER_TENSIX_ENABLE);
    if (rc < 0)
        printf("tensix_enable failed: tt_errno=%d\n", tt_errno);
    tt_close(sess);
}
