// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "trap.h"

#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <unistd.h>

// Signals sent with `raise()` report `SI_TKILL` on Linux
#ifndef SI_TKILL
#define SI_TKILL SI_USER
#endif

_Thread_local sigjmp_buf tt_trap_jmp;

// Guarded address range, empty while disarmed.
//
// Volatile, so arming and disarming stay ordered around the guarded access,
// and the handler reads what the interrupted code last stored.
static _Thread_local const volatile uint8_t *volatile armed_ptr = NULL;
static _Thread_local volatile size_t armed_len                  = 0;

// Previously installed handler, chained for unrelated faults
static struct sigaction prev;
static pthread_once_t once = PTHREAD_ONCE_INIT;

/// Handle `SIGBUS` for guarded accesses.
///
/// The signal is synchronous, so it is delivered on the faulting thread
/// and the thread-local armed range identifies whether the fault is ours.
static void handler(int sig, siginfo_t *info, void *ctx) {
    // Let a re-raised fault happen again.
    //
    // A handler installed after this one, such as Python's `faulthandler`,
    // may restore this one and re-raise the signal to its own thread, which
    // then carries no fault address. Returning lets the access fault again
    // with its address.
    if (armed_ptr != NULL && info->si_code == SI_TKILL &&
        info->si_pid == getpid())
        return;

    // Recover a fault inside the armed range
    const uint8_t *addr = info->si_addr;
    const uint8_t *base = (const uint8_t *)armed_ptr;
    if (base != NULL && addr >= base && addr < base + armed_len) {
        armed_ptr = NULL;
        siglongjmp(tt_trap_jmp, 1);
    }

    // Chain to the previous handler
    if (prev.sa_handler != SIG_DFL && prev.sa_handler != SIG_IGN) {
        if (prev.sa_flags & SA_SIGINFO)
            prev.sa_sigaction(sig, info, ctx);
        else
            prev.sa_handler(sig);
        return;
    }

    // Restore the default disposition and re-raise.
    //
    // A fault is fatal even with `SIGBUS` ignored, so an ignored disposition
    // takes the default action as well.
    struct sigaction dfl = { .sa_handler = SIG_DFL };
    sigemptyset(&dfl.sa_mask);
    sigaction(SIGBUS, &dfl, NULL);
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
