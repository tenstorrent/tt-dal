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
    // Bind maps the window into process address space
    tt_tlb_config_t cfg = { .addr = 0, .x_end = 1, .y_end = 1 };
    assert(tt_tlb_bind(sess, &tlb, &cfg) == 0);
    assert(tlb.ptr != NULL);
    assert(tlb.len > 0);

    assert(tt_tlb_free(sess, &tlb) == 0);
    assert(tt_close(sess) == 0);
}
