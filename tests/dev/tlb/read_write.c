// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "common.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
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

    // Read a value, write it back, then read again to verify round-trip
    uint32_t val_read1;
    memcpy(&val_read1, tlb.ptr, sizeof(val_read1));
    memcpy(tlb.ptr, &val_read1, sizeof(val_read1));
    uint32_t val_read2;
    memcpy(&val_read2, tlb.ptr, sizeof(val_read2));
    assert(val_read1 == val_read2);

    assert(tt_tlb_free(sess, &tlb) == 0);
    assert(tt_close(sess) == 0);
}
