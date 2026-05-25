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
    // Wake up the GDDR PHY
    int rc = tt_power(sess, TT_POWER_MRISC_PHY_WAKEUP);
    if (rc < 0)
        printf("mrisc_phy_wakeup failed: tt_errno=%d\n", tt_errno);
    tt_close(sess);
}
