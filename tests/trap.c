// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <ttdal.h>
#include <unistd.h>

#include "trap.h"

/// Map a window that faults like one severed by a reset.
///
/// A shared mapping past the end of its file raises `SIGBUS` on access, as
/// a TLB mapping zapped by a reset does.
static tt_tlb_t severed(void) {
    long page  = sysconf(_SC_PAGESIZE);
    FILE *file = tmpfile();
    assert(file != NULL);
    assert(ftruncate(fileno(file), page) == 0);
    void *ptr =
        mmap(NULL, page, PROT_READ | PROT_WRITE, MAP_SHARED, fileno(file), 0);
    assert(ptr != MAP_FAILED);
    assert(ftruncate(fileno(file), 0) == 0);
    return (tt_tlb_t){ .ptr = ptr, .len = (size_t)page };
}

/// Access the window's first word under the guard, as telemetry does.
static int guarded(const tt_tlb_t *tlb, bool write) {
    volatile uint32_t *word = tlb->ptr;
    tt_trap_arm(tlb->ptr, tlb->len);
    if (sigsetjmp(tt_trap_jmp, 1))
        return errno = ECONNRESET, -1;
    if (write)
        *word = 0;
    else
        (void)*word;
    tt_trap_disarm();
    return 0;
}

/// Check guarded accesses report the fault instead of raising it.
static void check_recovers(const tt_tlb_t *tlb) {
    assert(guarded(tlb, false) < 0);
    assert(errno == ECONNRESET);
    assert(guarded(tlb, true) < 0);
    assert(errno == ECONNRESET);
}

/// Check the calling thread blocks neither `SIGBUS` nor `SIGINT`.
static void check_unblocked(void) {
    sigset_t mask;
    assert(pthread_sigmask(SIG_BLOCK, NULL, &mask) == 0);
    assert(!sigismember(&mask, SIGBUS));
    assert(!sigismember(&mask, SIGINT));
}

/// Run `body` in a child, returning how it ended.
static int in_child(void (*body)(const tt_tlb_t *), const tt_tlb_t *tlb) {
    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        alarm(5); // a fault that repeats forever ends here instead
        body(tlb);
        _exit(0);
    }
    int status;
    assert(waitpid(pid, &status, 0) == pid);
    return status;
}

/// Fault outside any guarded access.
static void fault(const tt_tlb_t *tlb) {
    tt_trap_arm(tlb->ptr, 0); // installs the guard
    tt_trap_disarm();
    (void)*(volatile uint8_t *)tlb->ptr;
}

/// Fault outside any guarded access, with `SIGBUS` ignored beforehand.
static void fault_ignored(const tt_tlb_t *tlb) {
    signal(SIGBUS, SIG_IGN);
    fault(tlb);
}

/// Exit with a known status from a handler installed beforehand.
static void exit_handler(int sig) {
    (void)sig;
    _exit(42);
}

/// Fault outside any guarded access, with a handler installed beforehand.
static void fault_handled(const tt_tlb_t *tlb) {
    signal(SIGBUS, exit_handler);
    fault(tlb);
}

// The guard's own handler, saved while another is installed over it
static struct sigaction guard;

/// Call the guard's handler directly with every signal blocked.
///
/// Runtimes such as Go install handlers that forward this way.
static void forward(int sig, siginfo_t *info, void *ctx) {
    guard.sa_sigaction(sig, info, ctx);
}

/// Restore the guard's handler and raise the signal again.
///
/// Python's `faulthandler` does this after printing its traceback.
static void reraise(int sig, siginfo_t *info, void *ctx) {
    (void)info;
    (void)ctx;
    sigaction(SIGBUS, &guard, NULL);
    raise(sig);
}

/// Install `fn` over the guard's handler, blocking every signal while it
/// runs if `block` is set.
static void cover(void (*fn)(int, siginfo_t *, void *), int flags, bool block) {
    struct sigaction act = { .sa_sigaction = fn,
                             .sa_flags     = SA_SIGINFO | flags };
    if (block)
        sigfillset(&act.sa_mask);
    else
        sigemptyset(&act.sa_mask);
    assert(sigaction(SIGBUS, &act, &guard) == 0);
}

int main(void) {
    tt_tlb_t tlb = severed();

    // An unguarded fault still kills the process.
    //
    // Run first in children, so each installs the guard over its own
    // disposition.
    int status = in_child(fault, &tlb);
    assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGBUS);
    status = in_child(fault_ignored, &tlb);
    assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGBUS);

    // An unguarded fault reaches a handler installed beforehand
    status = in_child(fault_handled, &tlb);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 42);

    // A guarded fault is reported, leaving signals unblocked
    check_recovers(&tlb);
    check_unblocked();

    // A handler forwarding with signals blocked leaves them unblocked
    cover(forward, 0, true);
    check_recovers(&tlb);
    check_unblocked();
    assert(sigaction(SIGBUS, &guard, NULL) == 0);

#ifdef __linux__
    // A handler re-raising after restoring the guard is recovered.
    //
    // Only Linux reports the re-raise as `SI_TKILL`, which the guard matches.
    cover(reraise, SA_NODEFER, false);
    check_recovers(&tlb);
    check_unblocked();
#endif
}
