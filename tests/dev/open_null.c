// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

#include <assert.h>
#include <ttdal.h>

int main(void) {
    // NULL pointers must be rejected
    tt_device_t dev = { .id = 0 };
    tt_session_t sess;
    assert(tt_open(NULL, &sess) < 0);
    assert(tt_errno == TT_EINVAL);
    assert(tt_open(&dev, NULL) < 0);
    assert(tt_errno == TT_EINVAL);
}
