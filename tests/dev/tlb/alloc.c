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
    tt_tlb_t tlb;
    assert(tt_tlb_alloc(sess, TT_TLB_2MB, TT_TLB_WC, &tlb) == 0);
    // ptr must remain NULL until configured
    assert(tlb.ptr == NULL);
    assert(tlb.len == TT_TLB_2MB);
    assert(tt_tlb_free(sess, &tlb) == 0);
    assert(tt_close(sess) == 0);
}
