# Quirks

This file compiles implementation quirks that most users never encounter.
Each entry is a deliberate limitation or side effect of the current
implementation, not part of any function's contract.

## Device scan bound

`tt_dev_from_bdf()` matches against an internal scan buffer of 64 devices,
so a matching device beyond the first 64 is missed and reports `ENODEV`.
The bound comfortably exceeds the device count of any current system. File
a bug if your deployment approaches it.

## SMC poll cadence

`tt_smc_wait()` spins on the response for a few polls before sleeping a
millisecond between them. Both the spin count and the sleep are implementation
details tuned to observed firmware turnaround, not part of the contract, and
either may change. On a Blackhole running firmware bundle 18.x the reply lands
on the second poll, inside the spin, so a whole exchange costs about 40
microseconds. Code that needs a specific polling rate should drive
`tt_smc_poll()` itself.

## Uninitialized devices

A driver that considers a device uninitialized answers almost every `ioctl` with
`ENODEV`, which this library reports as `ECONNRESET`. Only device and driver
info queries and a reset are permitted in that state. The `ENODEV` to
`ECONNRESET` mapping assumes `ENODEV` on an open session means the descriptor
was invalidated by a reset or a removal, and that assumption does not hold here,
so a device that never finished initializing looks like one that vanished.
Observed on `tt-kmd` 2.9.0, where `tt_power()`, `tt_telemetry()`,
`tt_tlb_alloc()`, and every `tt_smc_*()` call returned `ECONNRESET` while
`tt_dev_info()` succeeded.

A reset that fails with `EIO` or `ETIMEDOUT` leaves the device in this state
until a later reset completes. Reopening the session does not help, since
requests on a new descriptor are refused the same way.

## Signal handler

The first `tt_telemetry()` call installs a process-wide `SIGBUS` handler
so a window zapped by an out-of-band reset reports `ECONNRESET` instead of
crashing. Faults outside the library's guarded reads chain to the
previously installed handler and crash as usual.

## Unfenced reset on old drivers

`tt-kmd` releases before 2.10 silently ignore `O_EXCL` on the device file, so
`TT_OPEN_EXCL` keeps no other client out. An unforced `tt_reset()` opens the
device with `TT_OPEN_EXCL` even while other clients hold it, and
`tt_reset_with()` accepts any session opened with `TT_OPEN_EXCL`, although other
clients may hold the device too. Either reset then invalidates those clients'
sessions. The driver version is not checked at runtime.

## Reset versus removal

The driver reports both an out-of-band reset and a physical removal as
`ENODEV` on a stale descriptor, so the library cannot tell them apart at
failure time and reports `ECONNRESET` for both. Reopening distinguishes
them: the open succeeds after a reset and reports `ENODEV` after a
removal. Differentiating the two in the driver is a candidate upstream
change.

## Toolchain floor

The header uses C23 attributes, so consumers need a C23 toolchain (or
C++17 for C++ consumers). A compatibility macro that degrades gracefully
on older toolchains is ready to adopt. File a bug if the floor blocks
you.
