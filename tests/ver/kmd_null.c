// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <assert.h>
#include <errno.h>
#include <ttdal.h>

int main(void) {
    // NULL output pointer must be rejected
    assert(tt_kmd_version(NULL) < 0);
    assert(errno == EINVAL);
}
