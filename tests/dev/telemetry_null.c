// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <assert.h>
#include <errno.h>
#include <ttdal.h>

int main(void) {
    tt_telemetry_t table[1];
    // NULL session pointer must be rejected
    assert(tt_telemetry(NULL, 1, table) < 0);
    assert(errno == EINVAL);
}
