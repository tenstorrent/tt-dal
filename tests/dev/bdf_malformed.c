// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <ttdal.h>

int main(void) {
    // Addresses in neither accepted format must be rejected
    const char *addrs[] = { "", "bdf", "03:00", "0000:03:00" };
    for (size_t i = 0; i < sizeof(addrs) / sizeof(addrs[0]); i++) {
        tt_device_t dev;
        assert(tt_dev_from_bdf(addrs[i], &dev) < 0);
        assert(errno == EINVAL);
    }
}
