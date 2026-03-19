// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

#include <assert.h>
#include <ttdal.h>

int main(void) {
    tt_message_t msg = { 0 };
    // NULL device pointer must be rejected
    assert(tt_message(NULL, &msg, false, 0) < 0);
    assert(tt_errno == TT_EINVAL);

    // NULL message pointer must also be rejected
    tt_device_t dev = { .id = 0, .fd = -1 };
    assert(tt_message(&dev, NULL, false, 0) < 0);
    assert(tt_errno == TT_EINVAL);
}
