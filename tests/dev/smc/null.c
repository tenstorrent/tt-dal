// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <assert.h>
#include <errno.h>
#include <ttdal.h>

int main(void) {
    tt_smc_msg_t msg = { 0 };
    // NULL session pointer must be rejected
    assert(tt_smc_call(NULL, &msg, &msg, 0) < 0);
    assert(errno == EINVAL);
    assert(tt_smc_post(NULL, &msg) < 0);
    assert(errno == EINVAL);
    assert(tt_smc_poll(NULL, &msg) < 0);
    assert(errno == EINVAL);
    assert(tt_smc_wait(NULL, &msg, 0) < 0);
    assert(errno == EINVAL);
    assert(tt_smc_drop(NULL) < 0);
    assert(errno == EINVAL);

    // NULL message pointers must also be rejected
    tt_session_t sess = { .dev = { .id = 0 }, .fd = -1 };
    assert(tt_smc_call(&sess, NULL, &msg, 0) < 0);
    assert(errno == EINVAL);
    assert(tt_smc_call(&sess, &msg, NULL, 0) < 0);
    assert(errno == EINVAL);
    assert(tt_smc_post(&sess, NULL) < 0);
    assert(errno == EINVAL);
    assert(tt_smc_poll(&sess, NULL) < 0);
    assert(errno == EINVAL);
    assert(tt_smc_wait(&sess, NULL, 0) < 0);
    assert(errno == EINVAL);
}
