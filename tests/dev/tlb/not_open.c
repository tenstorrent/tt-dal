// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

#include <assert.h>
#include <ttdal.h>

int main(void) {
    // Unopened device (fd = -1) must be rejected
    tt_device_t dev = { .id = 0, .fd = -1 };
    tt_tlb_t tlb;
    assert(tt_tlb_alloc(&dev, TT_TLB_2MB, TT_TLB_WC, &tlb) < 0);
    assert(tt_errno == TT_ENOTOPEN);
}
