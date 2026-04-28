// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

#include <assert.h>
#include <ttdal.h>

int main(void) {
    // Unopened device (fd = -1) must be rejected
    tt_device_t dev = { .id = 0, .fd = -1 };
    tt_telemetry_t table;
    assert(tt_telemetry(&dev, table) < 0);
    assert(tt_errno == TT_ENOTOPEN);
}
