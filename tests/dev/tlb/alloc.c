// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

#include "common.h"
#include <assert.h>
#include <ttdal.h>

// MARK: hardware
int main(void) {
    tt_device_t devs[1];
    tt_device_t *dev = open_test_device(devs);
    if (!dev)
        return EXIT_FAILURE;
    tt_tlb_t tlb;
    assert(tt_tlb_alloc(dev, TT_TLB_2MB, TT_TLB_WC, &tlb) == 0);
    // ptr must remain NULL until configured
    assert(tlb.ptr == NULL);
    assert(tlb.len == TT_TLB_2MB);
    tt_tlb_free(dev, &tlb);
    tt_dev_close(dev);
}
