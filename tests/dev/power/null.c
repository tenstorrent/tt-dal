// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

#include <assert.h>
#include <ttdal.h>

int main(void) {
    // NULL device pointer must be rejected
    assert(tt_power(NULL, 0) < 0);
    assert(tt_errno == TT_EINVAL);
}
