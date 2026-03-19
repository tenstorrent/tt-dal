// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

#include <assert.h>
#include <ttdal.h>

int main(void) {
    // NULL pointer must be rejected
    assert(tt_dev_close(NULL) < 0);
    assert(tt_errno == TT_EINVAL);
}
