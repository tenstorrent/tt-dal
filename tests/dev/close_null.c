// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

#include <assert.h>
#include <errno.h>
#include <ttdal.h>

int main(void) {
    // NULL pointer must be rejected
    assert(tt_close(NULL) < 0);
    assert(errno == EINVAL);
}
