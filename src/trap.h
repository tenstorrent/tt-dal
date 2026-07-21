// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

#ifndef TT_DAL_TRAP_H
#define TT_DAL_TRAP_H

#include <setjmp.h>
#include <stddef.h>

/// Thread-local recovery point for guarded device memory access.
///
/// Arm a range with `tt_trap_arm()`, then take the recovery point with
/// `sigsetjmp(tt_trap_jmp, 1)`. A `SIGBUS` raised by an access inside the
/// armed range resumes at the recovery point returning nonzero, with the
/// guard disarmed.
extern _Thread_local sigjmp_buf tt_trap_jmp;

/// Arm the `SIGBUS` guard for the given address range.
///
/// The first call installs a process-wide `SIGBUS` handler. Faults outside
/// an armed range chain to the previously installed handler, preserving
/// crash semantics for faults this library does not own.
///
/// @param ptr  Base of the guarded range.
/// @param len  Length of the guarded range in bytes.
void tt_trap_arm(const volatile void *ptr, size_t len);

/// Disarm the `SIGBUS` guard.
///
/// Call once the guarded access completes without faulting.
void tt_trap_disarm(void);

#endif /* TT_DAL_TRAP_H */
