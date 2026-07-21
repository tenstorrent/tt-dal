// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

#include <assert.h>
#include <errno.h>
#include <ttdal.h>

int main(void) {
    tt_message_t msg = { 0 };
    // NULL session pointer must be rejected
    assert(tt_message(NULL, &msg, false, 0) < 0);
    assert(errno == EINVAL);

    // NULL message pointer must also be rejected
    tt_session_t sess = { .dev = { .id = 0 }, .fd = -1 };
    assert(tt_message(&sess, NULL, false, 0) < 0);
    assert(errno == EINVAL);
}
