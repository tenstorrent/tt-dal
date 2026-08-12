// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

#include <assert.h>
#include <ttdal.h>

// MARK: hardware
int main(void) {
    tt_version_t ver;
    int rc = tt_kmd_version(&ver);
    // Driver may not be installed; skip if unavailable
    if (rc == 0)
        assert(ver.major >= 2);
}
