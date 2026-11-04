// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <assert.h>
#include <errno.h>
#include <ttdal.h>

int main(void) {
    // NULL pointers must be rejected
    tt_device_t dev;
    assert(tt_dev_from_bdf(NULL, &dev) < 0);
    assert(errno == EINVAL);
    assert(tt_dev_from_bdf("0000:03:00.0", NULL) < 0);
    assert(errno == EINVAL);
}
