// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

#include <assert.h>
#include <ttdal.h>

int main(void) {
    tt_tlb_t tlb;
    // NULL session pointer must be rejected
    assert(tt_tlb_alloc(NULL, TT_TLB_2MB, TT_TLB_WC, &tlb) < 0);
    assert(tt_errno == TT_EINVAL);
}
