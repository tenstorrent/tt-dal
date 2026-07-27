// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

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
    // Request maximum AI clock frequency
    int rc = tt_power(sess, TT_POWER_MAX_AI_CLK);
    if (rc < 0)
        printf("max_ai_clk failed: errno=%d\n", errno);
    assert(tt_close(sess) == 0);
}
