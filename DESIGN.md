# Design

This document captures the architectural philosophy and major design decisions
for the tt-dal C API. The library replaces ad-hoc "microdriver" code (quick
scripts that directly perform hardware operations) with a stable, reusable
foundation.

## Principles

The library is built on three core pillars:

1. **Stateless**: No global state registries or caches. Each handle contains
   exactly what's needed to interact with hardware.
2. **Mechanism**: Hardware access without imposing policy decisions. Users
   control resource lifecycles, timing, and allocation strategies.
3. **Canonical**: Provide the correct and recommended way to perform a given
   operation.

### Goals

What this library strives to provide:

- **Optimal**: Operations must be as fast as possible. There should be no
  unnecessary overhead, hidden allocation, or redundant syscalls.
- **Safety**: Misuse should fail loudly and immediately, never silently. The API
  is designed to make incorrect usage hard and detectable.

### Non-Goals

What this library explicitly **does not** provide:

- High-level abstractions (see [UMD])
- Resource pooling or caching
- Automatic resource management
- Convenience abstractions

[umd]: https://github.com/tenstorrent/tt-umd

> [!NOTE]
>
> Much of this library's implementation is a thin layer over `tt-kmd`. The
> goal, however, is not to be a pure 1:1 wrapper. Where the kernel interface has
> oddities or requires specific sequencing, `tt-dal` smooths those over and
> exposes a cleaner interface. It prefers simplicity and correctness over
> mechanical faithfulness to the underlying IOCTL structure.

## Decisions

### Architecture

#### Device Representation

Devices are modeled with two transparent structs: a cheap *descriptor* and an
owned *session*.

```c
typedef struct tt_device {
    uint32_t id;       // Device number (0, 1, 2, ...)
} tt_device_t;

typedef struct tt_session {
    tt_device_t dev;   // Device descriptor
    int fd;            // File descriptor (-1 after close)
    uint16_t flags;    // Open flags, reused by tt_reopen()
} tt_session_t;
```

A `tt_device_t` is a copyable identifier with no associated resources. It is
suitable for scanning, sorting, and routing. To interact with hardware, the
caller obtains a `tt_session_t` via `tt_open()`, which holds the open file
descriptor for the lifetime of the session. Sessions are released with
`tt_close()`.

This design enables:

- **Stack allocation** without heap management.
- **Clear ownership** semantics (user owns the structs).
- **Cheap copying** of descriptors (shallow copies are valid).
- **Efficient reuse** of file descriptors across operations.

The alternative (opaque handles with internal registries) was rejected because
it introduces hidden state, requires allocation/deallocation APIs, and violates
the stateless principle.

> [!CAUTION]
>
> Device numbers are only reassigned when a device is removed and re-probed
> (e.g. an out-of-band remove/rescan). `tt_reset()` performs the reset in
> place, so the device number does not change and descriptors remain valid
> for reopening. Descriptors held across an out-of-band re-probe may go
> stale and must be refreshed via `tt_dev_scan()`.

#### Resource Handles

Resource handles are transparent structs containing a kernel ID and associated
metadata. For example, TLBs:

```c
typedef struct tt_tlb {
    uint32_t id;           // TLB identifier
    void *ptr;             // allocation pointer (NULL until bound)
    size_t len;            // TLB window size
    uint64_t idx;          // memory-map offset (internal)
} tt_tlb_t;
```

Resource handles are stack-allocated. Identifiers are designated per-device, so
two devices can independently use the same resource ID.

Users must carefully pair allocate/free calls and manage handle lifecycles. An
uninitialized or freed handle should not be reused.

**Benefit**: No hidden state, clear ownership, debuggable.

#### Architecture-Specific Code

Implementation for features that differ across architectures is handled with
explicit dispatch at each site that needs it (e.g. telemetry selects its
per-architecture constants with a `switch`), keeping differences visible and
localized.

#### Kernel Driver Requirement

**TL;DR**: Requires `tt-kmd` >= 2.10. Documented, not enforced at runtime.

The library depends on driver semantics introduced in `tt-kmd` 2.10:
open-time exclusive arbitration (`O_EXCL`) and reset-issuing file descriptors
surviving the reset generation bump. Older drivers silently ignore `O_EXCL`
on the device file, leaving `tt_reset()` unfenced.

Runtime version checks are policy, not mechanism, so the library performs
none. The requirement is documented here, in the README, and as comments at
each site in the source that depends on a versioned driver feature.

### API Conventions

#### Error Handling Strategy

**TL;DR**: Functions return `-1` on error and leave the cause in `errno`,
exactly like libc.

The library defines no error space of its own. Errors from the OS propagate
untouched unless a standard value names the failure more precisely, in which
case the library reports that value instead. Callers see familiar `errno`
names (`ENODEV`, `EAGAIN`) rather than a private error vocabulary. The library
raises standard `errno` values for the failures it detects itself: `EINVAL`
(bad argument), `ENOTCONN` (session not open), `ENODEV` (device not found,
normalized from device lookups), `EIO` (device I/O failed, also raised for
driver soft-failures), `ETIMEDOUT` (reset did not complete), and `ENOTSUP`
(unsupported device architecture).

One value is normalized rather than propagated: a session operation that
fails with `ENODEV` had its descriptor invalidated by an out-of-band reset
or removal, which reports as `ECONNRESET`. A telemetry read whose window is
zapped mid-operation reports the same. The normalization is sound because
the caller holds proof the device existed, and it deliberately conflates
reset with removal. Reopening distinguishes them: the open succeeds after a
reset and reports `ENODEV` after a removal, and reconnecting never yields
`ECONNRESET`.

> [!NOTE]
>
> The language bindings layer policy above this mechanism: a session opened
> as persistent transparently reopens and retries the failing operation
> when it reports `ECONNRESET`, up to a small bounded number of attempts.
> The reopen never yields `ECONNRESET` itself. It either succeeds, making
> the reset invisible, or it fails exactly as `tt_open()` fails, with
> `ENODEV` for a dead device. A connection that keeps resetting past the
> bound also reports `ENODEV`, so persistent callers never see
> `ECONNRESET`. The C API never reopens on its own.

Per the libc convention, `errno` is meaningful only after a `-1` return. A
successful call may leave unrelated residue there, so callers must check the
return value before inspecting `errno`.

**Error setting pattern**:
```c
// Record a library-detected failure and return -1. Syscall failures
// propagate errno by returning TT_ERR directly, and session operations
// route through tt_fail_io(errno) for the ECONNRESET normalization.
return tt_fail(EINVAL);
```

**Error checking pattern**:
```c
// Check for failure, then inspect errno
if (tt_open(&dev, &sess, 0) < 0) {
    fprintf(stderr, "Open failed: %s\n", strerror(errno));
}
```

#### Unused Results

**TL;DR**: Every status return is `[[nodiscard]]`. Deliberate discards cast
to `void`.

Every function whose return value carries information is annotated
`[[nodiscard]]`, so a discarded status is a compiler warning instead of a
silent bug. This follows the fail-loudly principle: forgetting to check
`tt_open()` cannot slip through a build. Where discarding is deliberate
(cleanup paths that must preserve an earlier `errno`), the call is cast to
`void` to record the intent.

The attribute requires C23, which the library already targets, and C++17
for C++ consumers of the header.

### Safety & Correctness

#### Memory Management

**TL;DR**: Library never calls `malloc()`. All memory is caller-provided or
kernel-mapped.

No API function ever performs heap allocation. All memory is provided by the
caller or mapped from kernel resources.

**Key properties**:

- All handles are **caller-allocated**, typically on the stack. The library
  never allocates or frees these structures.
- **Zero internal state**: No global registries, no caches, no hidden lookup
  tables.
- Every operation is independent, using only the handles and parameters
  provided.
- Every syscall and side effect is explicit: no lazy initialization or
  background allocations.

**Benefits**:

- Users have **complete control** over memory.
- Can reason precisely about resource usage.
- Avoiding hidden allocations means **no hidden failure modes**.
- "What you see is what you get."

#### Device Lifecycle

Devices follow an **explicit open/close model**. A `tt_device_t` is just an
identifier. To perform operations the caller opens a `tt_session_t` via
`tt_open()` and releases it with `tt_close()`. Operations never implicitly
open a session.

```c
tt_device_t dev = { .id = 0 };
tt_session_t sess;
if (tt_open(&dev, &sess, 0) < 0) {
    // Handle error
}

// Use session for multiple operations
tt_dev_info(&sess, &info);
tt_tlb_alloc(&sess, TT_TLB_2MB, TT_TLB_UC, &tlb);

// Explicit cleanup
tt_close(&sess);
```

This gives users control over when to release resources while avoiding repeated
open/close overhead on every operation. Functions fail with `ENOTCONN` if
called on a closed session.

Enumeration is not an error path: `tt_dev_scan()` reports an absent driver as
zero devices, since a machine without the driver has none. Callers that
require a device raise `ENODEV` themselves.

#### TLB Lifecycle Safety

**TL;DR**: Deferred mmap prevents accessing unbound memory. Fail-fast with
`SIGSEGV`.

TLB windows follow a strict **allocate → bind → free** lifecycle:

- `alloc`: Claims a kernel-allocated TLB.
- `bind`: Configures and maps the window to a NOC address.
- `free`: Unmaps and releases the allocated TLB.

> [!NOTE]
>
> The operation is named `tt_tlb_bind` rather than `tt_tlb_configure` because
> it *associates* (binds) a window to a specific device address, analogous to
> binding a socket to a network address. "Configure" implies adjusting internal
> settings; "bind" captures the association between the window and its target
> address, which is the essential operation here.

##### Memory Mapping

If `tt_tlb_alloc()` mapped the window immediately, the pointer would reference
undefined NOC address space until configuration. Users could accidentally
read or write through the pointer, causing silent data corruption or undefined
hardware behavior.

**Solution**: Defer `mmap` until `tt_tlb_bind`.

1. `tt_tlb_alloc()`: Allocates the TLB ID in the kernel, sets `ptr = NULL`.
2. `tt_tlb_bind()`: Maps the window, sets `ptr`, configures the NOC mapping.
3. User accesses `tlb.ptr`: Valid pointer to bound device memory.
4. `tt_tlb_free()`: Unmaps and clears the pointer to prevent further use.

If users attempt to use `ptr` before binding, they get immediate `SIGSEGV`
(fail-fast) rather than silent corruption.

##### Rebinding

When rebinding an already-mapped TLB, the implementation mmaps a new address
first, then unmaps the old one. This ensures the kernel cannot reuse the old
virtual address, invalidating any stale interior pointers users may have saved.
Those pointers will fault on use rather than silently accessing different device
memory.

**Tradeoff**: Rebind incurs unmap/remap overhead (~microseconds), but
prioritizes **safety over performance**. Users building TLB pools can amortize
allocation cost. Binding is expected to be infrequent relative to actual
device access.

#### Fault Recovery

**TL;DR**: Library-issued window reads survive an out-of-band reset by
reporting `ECONNRESET`. Everything else stays fail-stop.

An out-of-band reset zaps every mapping belonging to the device, so a later
access through a bound window faults. For reads the library itself performs
(telemetry), the fault is caught and the operation reports `ECONNRESET`, so
a monitoring loop survives a reset instead of dying mid-read. Faults the
library does not own chain to whatever handler was installed before it and
crash exactly as they always did.

**Rejected**: Remapping zapped windows to a dummy page. Reads would return
fabricated data and the program would keep computing on it. A crash is
preferable to silent corruption, so recovery exists only where the library
can abort the operation and report an error instead.

#### Reset Semantics

**TL;DR**: Reset always runs exclusively. `tt_reset()` acquires exclusive
access internally and fails with `EAGAIN` if the device is in use.
`tt_reset_with()` consumes an open session, then does the same.

Reset comes in two forms:

```c
int tt_reset(const tt_device_t *dev);   // No session required
int tt_reset_with(tt_session_t *sess);  // Consumes the session
```

**Exclusivity**: The kernel arbitrates device access at open time as a
reader/writer lock, where `O_EXCL` is the writer and plain opens are
readers (KMD 2.10 or later). `tt_reset()` opens the device with
`O_EXCL | O_NONBLOCK`, runs the full sequence (ASIC reset through
post-reset) on that descriptor, and closes it. Acquisition succeeds only
when no other client has the device open, so a reset never destroys another
client's session out from under it.

Acquisition does not wait: if the device is busy, reset fails with
`EAGAIN` rather than resetting under other clients or blocking
indefinitely (the kernel's blocking exclusive open can be starved by a
steady stream of plain opens). The issuing descriptor survives the driver's
reset generation bump, so the sequence runs on one fd with no close/reopen
window that would drop exclusivity.

**Consumption**: `tt_reset_with()` closes the session first (exclusive
acquisition requires the device to be idle, so the caller's own descriptor
must be released), then delegates to `tt_reset()`. The session is consumed
on all paths, success or failure, exactly as if `tt_close()` had been
called: a half-reset device behind a maybe-valid file descriptor is a
silent corruption hazard. Another client may open the device in the window
between the close and the exclusive acquisition, in which case the reset
fails with `EAGAIN`.

**In place**: The driver keeps the device instance alive across the reset,
so the device number does not change and descriptors remain valid for
reopening once the reset returns.

> [!NOTE]
>
> While a reset (or any future exclusive holder, e.g. a flasher) holds the
> device, all other opens block until it is released, and the kernel
> provides no bypass. Recovery from a hung exclusive holder is out-of-band
> (sysfs remove, BMC) and beyond this library.

**Philosophy**: Reset is inherently disruptive (it invalidates all TLBs and
device state). Rather than making a forced reset maximally available, the
API makes the safe reset the only reset: exclusivity is acquired internally
and refusal is loud. Force and blocking variants may be added later as
explicit opt-ins.

### API Design

#### Session Open Flags

**TL;DR**: `tt_open()` takes a flags bitmask mirroring `open(2)`. There are
no variant functions.

`tt_open()` mirrors `open(2)`: one function, with behavior selected by a
flags bitmask (`tt_open_flag_t`). `TT_OPEN_EXCL` requests the writer side of
the kernel's open-time arbitration, `TT_OPEN_NONBLOCK` converts either wait
into an immediate `EAGAIN`, and the plain open is just `flags == 0`.

- Flag values are library-owned bits, mapped onto `O_EXCL`/`O_NONBLOCK`
  internally. The platform's numeric values never enter the ABI.
- Unknown bits fail with `EINVAL`, so a future flag cannot silently no-op on
  an older library.
- There is no blocking-versus-nonblocking default to choose. Both kernel
  behaviors are exposed one-to-one, and callers compose them.

#### Power Management

**TL;DR**: Thin, stateless wrapper over the `SET_POWER_STATE` ioctl, exposing
the driver's power aggregation model directly.

The kernel driver tracks a power state contribution from each open file
descriptor. Contributions are aggregated across clients (OR for flags, MAX for
settings) and pushed to firmware. When a client closes its fd, its contribution
is removed.

Opening a device with `O_APPEND` (as `tt_open()` does) starts with an
all-off initial state, meaning the client must explicitly request power features
via `tt_power()`. This is the **power-aware client** model.

The API exposes this model directly through `tt_power()`, which takes a
bitmask of `tt_power_flag_t` features. Every defined flag is marked valid on
each request, so a request describes the client's complete desired state and
any unset flag is an explicit off, not a "don't care".

**Rationale**: The aggregation semantics and the `validity` encoding are
driver-level concepts. Wrapping them in higher-level presets (e.g., "low power",
"high performance") would impose policy. Instead, the library exposes the
mechanism and lets callers decide what to request.

#### Telemetry Snapshot API

**TL;DR**: Read entire telemetry table in one operation for consistency and
performance.

Telemetry is designed around **complete snapshots** rather than individual tag
queries. The API provides only `tt_telemetry()` which reads the entire
telemetry table in a single operation.

**Rationale**:

1. **Consistency**: Multiple reads of individual tags would produce mismatched
   data as device state changes between calls. A snapshot represents a single
   update cycle, verified against the firmware heartbeat and retried on a
   mismatch.
2. **Performance**: The whole table is read through one mapped window in a
   single pass. Exposing per-tag reads would encourage inefficient usage
   patterns.

Users who need only specific tags can read the full snapshot and ignore unused
values. The table is small enough that this is negligible overhead.

## Tradeoffs

These are deliberate design choices with considered tradeoffs:

### State Management

**Decision**: Stateless API with no global registries.

**Rejected**: Global registry tracking all devices and resources.

**Rationale**: Registries create hidden state that's hard to debug, require
locking for thread safety, and obscure lifecycle management. Stateless design
prioritizes simplicity and clarity at the cost of users tracking their own
metadata.

### Transparent Handles

**Decision**: Transparent struct handles with visible fields.

**Rejected**: Opaque handles with getter/setter APIs.

**Rationale**: Opaque handles require heap allocation, lifetime management APIs,
and hide state from debuggers. Transparent structs are C-idiomatic and directly
inspectable. Users may manipulate struct fields directly, which is acceptable
for a mechanism-only library.

### Memory Allocation

**Decision**: Stack-based API with caller-allocated handles. Library never calls
`malloc()` or performs dynamic allocation.

**Rejected**: Heap-allocated handles managed by library.

**Rationale**: Eliminating dynamic allocation removes entire classes of failure
modes (out-of-memory errors, memory leaks, use-after-free). Stack allocation
gives users complete control over memory layout and lifetime. Predictable memory
usage is critical for embedded systems and performance-sensitive code. The cost
is that users must manage handle storage themselves.

## Relationship to Other Layers

```text
╭─────────────────────────────────────╮
│  tt-umd (C++ layer)                 │
│  - Policy, abstractions             │
│  - High-level API design            │
│  - Opinionated object model         │
╰─────────────────────────────────────╯
                 ↑
╭─────────────────────────────────────╮
│  tt-dal (this library)              │ ← You are here
│  - Stateless, transparent           │
│  - Mechanism-only API design        │
│  - Runs in userland                 │
╰─────────────────────────────────────╯
                 ↑
╭─────────────────────────────────────╮
│  tt-kmd (kernel driver)             │
│  - Low-level hardware interface     │
│  - IOCTLs as primary API            │
│  - Runs in kernel-space             │
╰─────────────────────────────────────╯
```

**Position**:

- **Above KMD**: Direct consumer of kernel IOCTLs. Functionality maps to kernel
  capabilities with minimal added functionality (e.g. telemetry).
- **Below UMD**: Foundation for higher-level libraries. UMD provides stateful,
  object-oriented abstractions.

## Future Extensibility

**Architectures**:

- Add a dispatch case at each architecture-specific site.

**IOCTLs**:

- Update vendored IOCTL definitions.
- Add wrapper and follow existing patterns.

## Stability

Once stabilized (version 1.0.0), the library will maintain backwards
compatibility within major versions. Pre-1.0 releases have no compatibility
guarantees as the API is still evolving.

### Breaking Changes

The following changes are considered breaking changes, and require a major
version bump:

- Changing function signatures.
- Removing or renaming public API functions.
- Adding, removing, or reordering structure fields.
- Changing the `errno` reported for an existing failure.
- Removing or changing enum values.

Note: Adding fields to transparent, caller-allocated structs breaks ABI because
it changes `sizeof()` and invalidates existing stack allocations.

### Non-Breaking Changes

The following changes are permitted in minor or patch releases:

- Adding new functions.
- Reporting new `errno` values for new failure modes.
- Adding new enum members.
- Adding new architecture support.
- Bug fixes that don't change API behavior.

These changes maintain backwards compatibility and do not require recompilation.

### ABI Compatibility

Binary compatibility is maintained within major versions through careful
management of structure layouts, function signatures, and symbol visibility.

### Deprecation

Features marked deprecated will remain functional for at least one major version
cycle. Deprecated APIs will be annotated with compiler warnings directing users
to replacements.
