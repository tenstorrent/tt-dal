// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

#include "common.h"
#include <assert.h>
#include <ttdal.h>

// MARK: hardware
int main(void) {
    tt_session_t s;
    tt_session_t *sess = open_test_device(&s);
    if (!sess)
        return EXIT_FAILURE;

    tt_smc_msg_t req = { 0 };
    req.message[0]   = SMC_MSG_TEST;
    req.message[1]   = 0xCAFE;
    tt_smc_msg_t rsp = { 0 };
    int rc           = tt_smc_call(sess, &req, &rsp, 0);
    // Firmware may lack a message queue (old firmware); skip if unavailable.
    if (rc == 0) {
        assert(rsp.message[0] == 0);      // firmware status OK
        assert(rsp.message[1] == 0xCAFF); // argument plus one
        assert(req.message[1] == 0xCAFE); // request left untouched
    }
    assert(tt_close(sess) == 0);
}
