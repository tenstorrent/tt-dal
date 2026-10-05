// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#ifndef TT_DAL_CLOCK_H
#define TT_DAL_CLOCK_H

#include <stdint.h>
#include <time.h>

/// Elapsed milliseconds between two points on the monotonic clock.
///
/// The difference is taken in signed nanoseconds. Subtracting the fields as
/// unsigned would turn the negative nanosecond difference of an interval that
/// crosses a second boundary into a value near `UINT64_MAX`.
static inline uint64_t
timespec_diff_ms(const struct timespec *start, const struct timespec *end) {
    int64_t ns = (int64_t)(end->tv_sec - start->tv_sec) * 1000000000 +
                 (end->tv_nsec - start->tv_nsec);
    return (uint64_t)(ns / 1000000);
}

/// Elapsed milliseconds since `start` on the monotonic clock.
static inline uint64_t elapsed_ms(const struct timespec *start) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return timespec_diff_ms(start, &now);
}

#endif /* TT_DAL_CLOCK_H */
