// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

#include <assert.h>
#include <errno.h>
#include <ttdal.h>

int main(void) {
    // NULL session pointer must be rejected
    assert(tt_power(NULL, 0) < 0);
    assert(errno == EINVAL);
}
