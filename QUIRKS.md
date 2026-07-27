# Quirks

This file compiles implementation quirks that most users never encounter.
Each entry is a deliberate limitation or side effect of the current
implementation, not part of any function's contract.

## Device scan bound

`tt_dev_from_bdf()` matches against an internal scan buffer of 64 devices,
so a matching device beyond the first 64 is missed and reports `ENODEV`.
The bound comfortably exceeds the device count of any current system. File
a bug if your deployment approaches it.

## SMC messaging

`tt_message()` is unimplemented. Past its argument guards it aborts the
process rather than silently pretending to succeed.

## Signal handler

The first `tt_telemetry()` call installs a process-wide `SIGBUS` handler
so a window zapped by an out-of-band reset reports `ECONNRESET` instead of
crashing. Faults outside the library's guarded reads chain to the
previously installed handler and crash as usual.

## Unfenced reset on old drivers

`tt-kmd` releases before 2.10 silently ignore `O_EXCL` on the device file,
so `tt_reset()` cannot fence out other clients. The driver version is not
checked at runtime.

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
