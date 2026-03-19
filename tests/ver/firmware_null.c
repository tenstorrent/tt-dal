// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

#include <assert.h>
#include <ttdal.h>

int main(void) {
    tt_version_t ver;
    // NULL device pointer must be rejected
    assert(tt_version_firmware(NULL, &ver) < 0);
    assert(tt_errno == TT_EINVAL);
}
