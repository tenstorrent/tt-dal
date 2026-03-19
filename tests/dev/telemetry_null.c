// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

#include <assert.h>
#include <ttdal.h>

int main(void) {
    tt_telemetry_t table;
    // NULL device pointer must be rejected
    assert(tt_telemetry(NULL, table) < 0);
    assert(tt_errno == TT_EINVAL);

    // NULL table pointer must also be rejected
    tt_device_t dev = { .id = 0, .fd = -1 };
    assert(tt_telemetry(&dev, NULL) < 0);
    assert(tt_errno == TT_EINVAL);
}
