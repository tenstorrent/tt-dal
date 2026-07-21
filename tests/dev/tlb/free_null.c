// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

#include <assert.h>
#include <errno.h>
#include <ttdal.h>

int main(void) {
    tt_tlb_t tlb;
    // NULL session pointer must be rejected
    assert(tt_tlb_free(NULL, &tlb) < 0);
    assert(errno == EINVAL);
}
