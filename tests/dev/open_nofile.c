// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <sys/resource.h>
#include <ttdal.h>

int main(void) {
    // Exhaust descriptors.
    //
    // A zero soft limit leaves no descriptor to allocate. Both Linux and macOS
    // allocate the descriptor before resolving the path, so the open fails with
    // `EMFILE` whether or not the device exists.
    struct rlimit lim;
    assert(getrlimit(RLIMIT_NOFILE, &lim) == 0);
    rlim_t cur   = lim.rlim_cur;
    lim.rlim_cur = 0;
    assert(setrlimit(RLIMIT_NOFILE, &lim) == 0);

    // Failures other than an absent device must keep their own code
    tt_device_t dev = { .id = UINT32_MAX };
    tt_session_t sess;
    assert(tt_open(&dev, &sess, 0) < 0);
    assert(errno == EMFILE);

    // Restore descriptors
    lim.rlim_cur = cur;
    assert(setrlimit(RLIMIT_NOFILE, &lim) == 0);
}
