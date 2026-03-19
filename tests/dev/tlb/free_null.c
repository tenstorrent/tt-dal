// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

#include <assert.h>
#include <ttdal.h>

int main(void) {
    tt_tlb_t tlb;
    // NULL device pointer must be rejected
    assert(tt_tlb_free(NULL, &tlb) < 0);
    assert(tt_errno == TT_EINVAL);
}
