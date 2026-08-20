// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

#include "err.h"
#include "ioctl.h"
#include "ttdal.h"

#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define SPIN_POLLS 32         // back-to-back polls before sleeping between them
#define POLL_INTERVAL_US 1000 // sleep between polls once the spin is spent

/// Elapsed milliseconds since `start` on the monotonic clock.
static uint64_t elapsed_ms(const struct timespec *start) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)(now.tv_sec - start->tv_sec) * 1000 +
           (uint64_t)(now.tv_nsec - start->tv_nsec) / 1000000;
}

/// Call SMC with a message.
///
/// Posts the request, then waits for the response. Cancels the message on any
/// early return, leaving the session free to post again.
int tt_smc_call(
    const tt_session_t *sess,
    const tt_smc_msg_t *req,
    tt_smc_msg_t *rsp,
    uint32_t timeout
) {
    // Validate args
    if (!rsp)
        return tt_fail(EINVAL);

    // Post the request. Session and request checks happen here.
    if (tt_smc_post(sess, req) != TT_OK)
        return TT_ERR;

    // Wait for the response.
    if (tt_smc_wait(sess, rsp, timeout) != TT_OK) {
        int err = errno; // saved before any cleanup can clobber it

        // Discard the response. A consumed message is already gone, making
        // this a no-op, so the wait's error is what the caller sees.
        (void)tt_smc_drop(sess);
        return tt_fail(err);
    }

    return TT_OK;
}

/// Post an SMC message.
///
/// Copies the eight request words into the driver's per-session slot. The
/// message is outstanding until polled for or cancelled.
int tt_smc_post(const tt_session_t *sess, const tt_smc_msg_t *req) {
    // Validate args
    if (!sess || !req)
        return tt_fail(EINVAL);

    // Ensure session is open
    if (sess->fd < 0)
        return tt_fail(ENOTCONN);

    // Post the message
    struct tenstorrent_smc_msg post = {
        .argsz = sizeof(post),
        .flags = TENSTORRENT_SMC_MSG_POST,
    };
    memcpy(post.message, req->message, sizeof(req->message));
    if (ioctl(sess->fd, TENSTORRENT_IOCTL_SMC_MSG, &post) != 0)
        return tt_fail_io(errno);

    return TT_OK;
}

/// Poll for an SMC response.
///
/// Never cancels: an unready message is left outstanding for the caller to
/// poll again.
int tt_smc_poll(const tt_session_t *sess, tt_smc_msg_t *rsp) {
    // Validate args
    if (!sess || !rsp)
        return tt_fail(EINVAL);

    // Ensure session is open
    if (sess->fd < 0)
        return tt_fail(ENOTCONN);

    // Check for the response
    struct tenstorrent_smc_msg poll = {
        .argsz = sizeof(poll),
        .flags = TENSTORRENT_SMC_MSG_POLL,
    };
    if (ioctl(sess->fd, TENSTORRENT_IOCTL_SMC_MSG, &poll) != 0)
        return tt_fail_io(errno);

    // Response ready. The reply is only written once the exchange is known to
    // have completed, so a request sharing storage with it survives a retry.
    memcpy(rsp->message, poll.message, sizeof(rsp->message));
    return TT_OK;
}

/// Wait for an SMC response.
///
/// Spins on the response briefly, then polls on a fixed interval until it
/// arrives or the budget is spent. Never drops the message: one that outlives
/// the budget is left outstanding for the caller to wait on again.
int tt_smc_wait(const tt_session_t *sess, tt_smc_msg_t *rsp, uint32_t timeout) {
    // Validate args
    if (!sess || !rsp)
        return tt_fail(EINVAL);

    // Ensure session is open
    if (sess->fd < 0)
        return tt_fail(ENOTCONN);

    // Poll for the response.
    //
    // The controller usually answers well inside a millisecond, so the first
    // polls run back to back before falling back to sleeping between them.
    uint64_t budget = timeout ? timeout : TT_SMC_TIMEOUT_DEFAULT;
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (unsigned spins = 0;; spins++) {
        if (tt_smc_poll(sess, rsp) == TT_OK)
            return TT_OK;

        // A pending response is the only retryable outcome.
        if (errno != EAGAIN)
            return TT_ERR;

        // Give up once the poll budget is spent.
        if (elapsed_ms(&start) >= budget)
            return tt_fail(ETIMEDOUT);

        if (spins >= SPIN_POLLS)
            usleep(POLL_INTERVAL_US);
    }
}

/// Drop an outstanding SMC message.
///
/// The driver discards a response arriving after the drop, and treats a
/// session with nothing outstanding as a no-op. A message that already
/// reached the controller still runs.
int tt_smc_drop(const tt_session_t *sess) {
    // Validate args
    if (!sess)
        return tt_fail(EINVAL);

    // Ensure session is open
    if (sess->fd < 0)
        return tt_fail(ENOTCONN);

    // Drop the message
    struct tenstorrent_smc_msg abandon = {
        .argsz = sizeof(abandon),
        .flags = TENSTORRENT_SMC_MSG_ABANDON,
    };
    if (ioctl(sess->fd, TENSTORRENT_IOCTL_SMC_MSG, &abandon) != 0)
        return tt_fail_io(errno);

    return TT_OK;
}
