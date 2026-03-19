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
    tt_tlb_config_t cfg = { .addr = 0, .x_end = 1, .y_end = 1 };
    assert(tt_tlb_configure(dev, &tlb, &cfg) == 0);
    void *old_ptr = tlb.ptr;
    // Reconfiguring must remap to a new address, invalidating stale pointers
    assert(tt_tlb_configure(dev, &tlb, &cfg) == 0);
    assert(tlb.ptr != NULL);
    assert(tlb.ptr != old_ptr);

    tt_tlb_free(dev, &tlb);
    tt_dev_close(dev);
}
