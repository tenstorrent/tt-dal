// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

#ifndef TT_DAL_ERR_H
#define TT_DAL_ERR_H

#include <errno.h>

#include "ttdal.h"

/// Record a failure and return `TT_ERR`.
static inline int tt_fail(int err) {
    errno = err;
    return TT_ERR;
}

/// Record a failed session operation and return `TT_ERR`.
///
/// `err` is the `errno` of the failing operation, evaluated before any
/// cleanup (e.g. `close()`) that could clobber it. A session operation
/// that fails with `ENODEV` had its descriptor invalidated by an
/// out-of-band reset or removal, which reports as `ECONNRESET`.
///
/// The rewrite is sound because the caller holds proof the device
/// existed: `ENODEV` from an operation on a successfully opened session
/// cannot mean "no such device". `ECONNRESET` deliberately covers both
/// reset and removal. Reopening distinguishes them, succeeding after a
/// reset and failing with `ENODEV` after a removal, and reconnecting
/// never yields `ECONNRESET`.
static inline int tt_fail_io(int err) {
    return tt_fail(err == ENODEV ? ECONNRESET : err);
}

#endif /* TT_DAL_ERR_H */
