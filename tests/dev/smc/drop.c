// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "common.h"
#include <assert.h>
#include <ttdal.h>

// MARK: hardware
int main(void) {
    tt_session_t s;
    tt_session_t *sess = open_test_device(&s);
    if (!sess)
        return EXIT_FAILURE;

    // Dropping with nothing outstanding is a no-op.
    assert(tt_smc_drop(sess) == 0);

    tt_smc_msg_t req = { 0 };
    req.message[0]   = SMC_MSG_TEST;
    req.message[1]   = 0xCAFE;
    // Firmware may lack a message queue (old firmware); skip if unavailable.
    if (tt_smc_post(sess, &req) == 0) {
        // Discarding the response frees the session to post again.
        assert(tt_smc_drop(sess) == 0);
        assert(tt_smc_post(sess, &req) == 0);
        assert(tt_smc_drop(sess) == 0);
    }

    assert(tt_close(sess) == 0);
}
