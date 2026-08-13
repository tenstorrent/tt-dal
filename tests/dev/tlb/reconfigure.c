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

    tt_tlb_t tlb;
    assert(tt_tlb_alloc(sess, TT_TLB_2MB, TT_TLB_WC, &tlb) == 0);
    tt_tlb_config_t cfg = { .addr = 0, .x_end = 1, .y_end = 1 };
    assert(tt_tlb_bind(sess, &tlb, &cfg) == 0);
    void *old_ptr = tlb.ptr;
    // Reconfiguring must remap to a new address, invalidating stale pointers
    assert(tt_tlb_bind(sess, &tlb, &cfg) == 0);
    assert(tlb.ptr != NULL);
    assert(tlb.ptr != old_ptr);

    assert(tt_tlb_free(sess, &tlb) == 0);
    assert(tt_close(sess) == 0);
}
