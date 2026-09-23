// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "common.h"
#include <assert.h>
#include <errno.h>
#include <ttdal.h>

// MARK: hardware
int main(void) {
    tt_session_t s;
    tt_session_t *sess = open_test_device(&s);
    if (!sess)
        return EXIT_FAILURE;

    // Polling with nothing outstanding is caller misuse.
    tt_smc_msg_t rsp = { 0 };
    assert(tt_smc_poll(sess, &rsp) < 0);
    assert(errno == ESRCH);

    tt_smc_msg_t req = { 0 };
    req.message[0]   = SMC_MSG_TEST;
    req.message[1]   = 0xCAFE;
    // Firmware may lack a message queue (old firmware); skip if unavailable.
    if (tt_smc_post(sess, &req) == 0) {
        // An unready response leaves the message outstanding, so poll until it
        // arrives.
        int rc;
        do {
            rc = tt_smc_poll(sess, &rsp);
        } while (rc < 0 && errno == EAGAIN);
        assert(rc == 0);
        assert(rsp.message[0] == 0);      // firmware status OK
        assert(rsp.message[1] == 0xCAFF); // argument plus one

        // The response freed the session to post the same request again, this
        // time waiting for the reply instead of polling for it.
        assert(tt_smc_post(sess, &req) == 0);
        assert(tt_smc_wait(sess, &rsp, 0) == 0);
        assert(rsp.message[0] == 0);
        assert(rsp.message[1] == 0xCAFF);
    }

    assert(tt_close(sess) == 0);
}
