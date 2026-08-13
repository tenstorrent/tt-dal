// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "trap.h"

#include <pthread.h>
#include <signal.h>
#include <stdint.h>

_Thread_local sigjmp_buf tt_trap_jmp;

// Guarded address range, empty while disarmed
static _Thread_local const volatile uint8_t *armed_ptr = NULL;
static _Thread_local size_t armed_len                  = 0;

// Previously installed handler, chained for unrelated faults
static struct sigaction prev;
static pthread_once_t once = PTHREAD_ONCE_INIT;

/// Handle `SIGBUS` for guarded accesses.
///
/// The signal is synchronous, so it is delivered on the faulting thread
/// and the thread-local armed range identifies whether the fault is ours.
static void handler(int sig, siginfo_t *info, void *ctx) {
    // Recover a fault inside the armed range
    const uint8_t *addr = info->si_addr;
    const uint8_t *base = (const uint8_t *)armed_ptr;
    if (base != NULL && addr >= base && addr < base + armed_len) {
        armed_ptr = NULL;
        siglongjmp(tt_trap_jmp, 1);
    }

    // Chain to the previous handler
    if (prev.sa_flags & SA_SIGINFO)
        return prev.sa_sigaction(sig, info, ctx);
    if (prev.sa_handler == SIG_IGN)
        return;
    if (prev.sa_handler != SIG_DFL)
        return prev.sa_handler(sig);

    // Restore the default disposition and re-raise
    sigaction(SIGBUS, &prev, NULL);
    raise(SIGBUS);
}

/// Install the `SIGBUS` handler.
static void install(void) {
    struct sigaction act = {
        .sa_sigaction = handler,
        .sa_flags     = SA_SIGINFO,
    };
    sigemptyset(&act.sa_mask);
    sigaction(SIGBUS, &act, &prev);
}

void tt_trap_arm(const volatile void *ptr, size_t len) {
    pthread_once(&once, install);
    armed_ptr = ptr;
    armed_len = len;
}

void tt_trap_disarm(void) { armed_ptr = NULL; }
