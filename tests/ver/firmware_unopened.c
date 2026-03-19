// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

#include <assert.h>
#include <ttdal.h>

int main(void) {
    tt_version_t ver;
    // Unopened device (fd = -1) must be rejected
    tt_device_t dev = { .id = 0, .fd = -1 };
    assert(tt_version_firmware(&dev, &ver) < 0);
    assert(tt_errno == TT_ENOTOPEN);
}
