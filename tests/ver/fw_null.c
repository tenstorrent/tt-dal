// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <assert.h>
#include <errno.h>
#include <ttdal.h>

int main(void) {
    tt_version_t ver;
    // NULL session pointer must be rejected
    assert(tt_fw_version(NULL, &ver) < 0);
    assert(errno == EINVAL);
}
