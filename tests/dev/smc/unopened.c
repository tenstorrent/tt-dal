// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

#include <assert.h>
#include <errno.h>
#include <ttdal.h>

int main(void) {
    tt_smc_msg_t msg  = { 0 };
    // Unopened session (fd = -1) must be rejected
    tt_session_t sess = { .dev = { .id = 0 }, .fd = -1 };
    assert(tt_smc_call(&sess, &msg, &msg, 0) < 0);
    assert(errno == ENOTCONN);
    assert(tt_smc_post(&sess, &msg) < 0);
    assert(errno == ENOTCONN);
    assert(tt_smc_poll(&sess, &msg) < 0);
    assert(errno == ENOTCONN);
    assert(tt_smc_wait(&sess, &msg, 0) < 0);
    assert(errno == ENOTCONN);
    assert(tt_smc_drop(&sess) < 0);
    assert(errno == ENOTCONN);
}
