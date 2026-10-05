// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "clock.h"

#include <assert.h>

int main(void) {
    // Whole seconds
    struct timespec a = { .tv_sec = 10, .tv_nsec = 0 };
    struct timespec b = { .tv_sec = 15, .tv_nsec = 0 };
    assert(timespec_diff_ms(&a, &b) == 5000);

    // Sub-second
    struct timespec c = { .tv_sec = 10, .tv_nsec = 250000000 };
    assert(timespec_diff_ms(&a, &c) == 250);

    // Borrow across a second boundary
    struct timespec d = { .tv_sec = 10, .tv_nsec = 900000000 };
    struct timespec e = { .tv_sec = 11, .tv_nsec = 100000000 };
    assert(timespec_diff_ms(&d, &e) == 200);

    // Borrow across many seconds
    struct timespec f = { .tv_sec = 20, .tv_nsec = 100000000 };
    assert(timespec_diff_ms(&d, &f) == 9200);

    // Borrow with a sub-millisecond remainder
    struct timespec g = { .tv_sec = 10, .tv_nsec = 900000001 };
    assert(timespec_diff_ms(&g, &e) == 199);

    // Identical points
    assert(timespec_diff_ms(&a, &a) == 0);
}
