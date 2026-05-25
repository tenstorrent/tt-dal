// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

#include <assert.h>
#include <ttdal.h>

int main(void) {
    tt_version_t ver;
    // Unopened session (fd = -1) must be rejected
    tt_session_t sess = { .dev = { .id = 0 }, .fd = -1 };
    assert(tt_version_firmware(&sess, &ver) < 0);
    assert(tt_errno == TT_ENOTOPEN);
}
