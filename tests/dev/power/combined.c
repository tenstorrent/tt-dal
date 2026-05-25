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
    // Request all four defined power flags together
    int rc = tt_power(sess, 0x000F);
    if (rc < 0)
        printf("combined (0x000F) failed: tt_errno=%d\n", tt_errno);
    tt_close(sess);
}
