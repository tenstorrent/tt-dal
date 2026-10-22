// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <ttdal.h>

// MARK: hardware
int main(void) {
    tt_device_t dev;
    if (tt_dev_scan(1, &dev) <= 0)
        return EXIT_FAILURE;

    // Map a window through an exclusive session
    tt_session_t sess;
    assert(tt_open(&dev, &sess, TT_OPEN_EXCL) == 0);
    tt_tlb_t tlb;
    assert(tt_tlb_alloc(&sess, TT_TLB_2MB, TT_TLB_UC, &tlb) == 0);
    tt_tlb_config_t cfg = { .addr = 0, .x_end = 1, .y_end = 1 };
    assert(tt_tlb_bind(&sess, &tlb, &cfg) == 0);

    // Freeing after the session closed still unmaps the window
    assert(tt_close(&sess) == 0);
    assert(tt_tlb_free(&sess, &tlb) < 0);
    assert(errno == ENOTCONN);
    assert(tlb.ptr == NULL);

    // The driver released the descriptor, so the device is free again
    tt_session_t excl;
    assert(tt_open(&dev, &excl, TT_OPEN_EXCL | TT_OPEN_NONBLOCK) == 0);
    assert(tt_close(&excl) == 0);
}
