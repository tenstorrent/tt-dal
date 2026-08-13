// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

/**
 * @file ttdal.h
 * @brief Tenstorrent Device Access Library.
 *
 * This header defines a stateless, mechanism-only C API for accessing
 * Tenstorrent accelerator hardware. It is designed to be consumed by
 * higher-level libraries.
 *
 * The library holds no global state and takes no locks. A session wraps one
 * file descriptor and is not safe to use from more than one thread at a time.
 * Open a session per thread instead, which the driver supports and arbitrates.
 *
 * The header uses C23 attributes, so consumers need C23 (or C++17 for C++
 * consumers).
 *
 * @version 0.1.0
 * @copyright Copyright (c) 2026 Tenstorrent Inc.
 */

#ifndef TT_DAL_H
#define TT_DAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef struct tt_device tt_device_t;
typedef struct tt_session tt_session_t;

/*============================================================================*
 * VERSION                                                                    *
 *============================================================================*/

/// Major version definition.
#define TTDAL_VERSION_MAJOR 0
/// Minor version definition.
#define TTDAL_VERSION_MINOR 1
/// Patch version definition.
#define TTDAL_VERSION_PATCH 0

/// Semantic version.
typedef struct tt_version {
    /// Major version number.
    uint64_t major;
    /// Minor version number.
    uint64_t minor;
    /// Patch version number.
    uint64_t patch;
    /// Pre-release identifier.
    char pre[32];
    /// Build metadata identifier.
    char build[32];
} tt_version_t;

/// Get the library version at compile time.
///
/// Use this to verify ABI compatibility.
///
/// @return  Library version.
[[nodiscard]] static inline tt_version_t tt_version(void) {
    return (tt_version_t){
        .major = TTDAL_VERSION_MAJOR,
        .minor = TTDAL_VERSION_MINOR,
        .patch = TTDAL_VERSION_PATCH,
    };
}

/// Get the KMD version.
///
/// The kernel-mode driver (KMD) version is a property of the kernel module,
/// not a specific device.
/// This function discovers an available device, queries the version through
/// it, and closes it before returning.
///
/// @param[out] version  KMD version output.
/// @return              0 on success, -1 on error (check `errno`).
///
/// @par Errors
///
/// * `EINVAL`     `version` is `NULL`.
/// * `ENODEV`     No device is available.
/// * `ECONNRESET` The device was reset or removed out-of-band.
///
/// Other codes propagate from the failing system call.
[[nodiscard]] int tt_kmd_version(tt_version_t *version);

/// Get the device firmware version.
///
/// Reads the firmware bundle version from a per-device sysfs attribute. A
/// non-zero release candidate maps to an `rc.N` pre-release identifier.
///
/// @param sess          Session handle.
/// @param[out] version  Firmware version output.
/// @return              0 on success, -1 on error (check `errno`).
///
/// @par Errors
///
/// * `EINVAL`     `sess` or `version` is `NULL`.
/// * `ENOTCONN`   The session is not open.
/// * `EIO`        The version string was empty or malformed.
///
/// Other codes propagate from the failing system call.
[[nodiscard]] int
tt_fw_version(const tt_session_t *sess, tt_version_t *version);

/*============================================================================*
 * ERRORS                                                                     *
 *============================================================================*/

/// Return value indicating success.
#define TT_OK 0

/// Return value indicating failure.
#define TT_ERR (-1)

/*
 * Functions return -1 on error and leave the cause in `errno`, following
 * the libc convention. Stringify with `strerror()` or report with
 * `perror()`. The value in `errno` is meaningful only after a -1 return,
 * as a successful call may leave unrelated residue there.
 *
 * Errors from the OS propagate untouched unless a standard value names
 * the failure more precisely, in which case the library reports that
 * value instead:
 *
 * * `EINVAL`     Invalid argument.
 * * `ENOTCONN`   Session is not open.
 * * `ENODEV`     No such device (normalized from device lookups).
 * * `EAGAIN`     Device held by another client (non-blocking open).
 * * `ECONNRESET` Device connection reset. The session was severed by an
 *                out-of-band reset or removal (normalized from `ENODEV`
 *                on session operations). Reopen to continue: the open
 *                succeeds after a reset and reports `ENODEV` after a
 *                removal.
 * * `EIO`        Device I/O failed (also raised for driver
 *                soft-failures).
 * * `ETIMEDOUT`  Reset did not complete in time.
 * * `ENOTSUP`    Operation not supported on this device architecture.
 */

/*============================================================================*
 * DEVICE                                                                     *
 *============================================================================*/

/// Device architecture.
///
/// Architecture specifier for a Tenstorrent device architecture generation.
/// Values are assigned their corresponding PCI device IDs.
typedef enum tt_arch {
    /// Grayskull.
    ///
    /// PCIe device ID: `0xffa0`.
    ///
    /// @note This is a legacy architecture that is no longer supported.
    TT_ARCH_GRAYSKULL [[deprecated]] = 0xffa0,
    /// Wormhole.
    ///
    /// PCIe device ID: `0x401e`.
    TT_ARCH_WORMHOLE                 = 0x401e,
    /// Blackhole.
    ///
    /// PCIe device ID: `0xb140`.
    TT_ARCH_BLACKHOLE                = 0xb140,
} tt_arch_t;

/// Get architecture name as string.
///
/// @param arch  Architecture variant.
/// @return      Architecture string, or `NULL` if invalid.
[[nodiscard]] static inline const char *tt_arch_describe(tt_arch_t arch) {
    switch (arch) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
        case TT_ARCH_GRAYSKULL:
            return "Grayskull";
#pragma GCC diagnostic pop
        case TT_ARCH_WORMHOLE:
            return "Wormhole";
        case TT_ARCH_BLACKHOLE:
            return "Blackhole";
        default:
            return NULL;
    }
}

/// Device descriptor.
///
/// A small, copyable struct describing a device. Holds no open resources.
///
/// Obtain via `tt_dev_scan()`, `tt_dev_from_path()`, or
/// `tt_dev_from_bdf()`. To perform operations on the device, open a session
/// handle with `tt_open()`.
typedef struct tt_device {
    /// Device identifier.
    ///
    /// Uniquely identifies a device. Suitable for comparisons, as multiple
    /// descriptors for the same device will share this identifier.
    ///
    /// TODO: This value should remain stable after a reset, although the
    /// current implementation does not reflect this. This could be accomplished
    /// by using a driver-provided UUID rather than using the device number.
    ///
    /// NOTE: This choice of type is unstable, and may be changed without
    /// warning before the initial release.
    uint32_t id;
} tt_device_t;

/// Open session handle.
///
/// An owned handle that holds an open file descriptor to a device. Required to
/// perform any operation that interacts with hardware.
///
/// Obtain via `tt_open()`; release via `tt_close()`, or by passing to
/// `tt_reset_with()`, which consumes the session. A handle is also
/// invalidated by an out-of-band device reset or removal, after which
/// `tt_reopen()` restores it in place.
typedef struct tt_session {
    /// Device descriptor for this session.
    tt_device_t dev;
    /// File descriptor.
    ///
    /// Underlying kernel fd used for `ioctl` and `mmap`. `-1` after
    /// `tt_close()` to detect use-after-close.
    int fd;
    /// Open flags.
    ///
    /// Bitmask of `tt_open_flag_t` values the session was opened with,
    /// reused by `tt_reopen()`.
    uint16_t flags;
} tt_session_t;

/// Create device from a path.
///
/// The path is resolved through symlinks, so `by-id/` entries work.
///
/// Must call `tt_open()` to obtain a session before using the device.
///
/// @param path      Device path.
/// @param[out] dev  Device descriptor to initialize.
/// @return          0 on success, -1 on error (check `errno`).
///
/// @par Errors
///
/// * `EINVAL`     `path` or `dev` is `NULL`, or `path` is not a device node.
/// * `ENODEV`     The path does not resolve to a device.
///
/// @par Example
///
/// ```c
/// tt_device_t dev;
/// if (tt_dev_from_path("/dev/tenstorrent/0", &dev) < 0) {
///     // Handle error
/// }
/// tt_session_t sess;
/// tt_open(&dev, &sess, 0);
/// // ... use session ...
/// tt_close(&sess);
/// ```
[[nodiscard]] int tt_dev_from_path(const char *path, tt_device_t *dev);

/// Create device from a PCIe bus/device/function (BDF) address.
///
/// Scans connected devices and initializes the descriptor for the device
/// matching the given BDF. Accepts `DDDD:BB:DD.F` or
/// `BB:DD.F` format. Each
/// scanned device is briefly opened to read and compare its BDF.
///
/// Must call `tt_open()` to obtain a session before using the device.
///
/// @param addr      PCIe BDF string (e.g. "0000:03:00.0" or "03:00.0").
/// @param[out] dev  Device descriptor to initialize.
/// @return          0 on success, -1 on error (check `errno`).
///
/// @par Errors
///
/// * `EINVAL`     `addr` or `dev` is `NULL`, or `addr` is malformed.
/// * `ENODEV`     No device matches the address.
///
/// @par Example
///
/// ```c
/// tt_device_t dev;
/// if (tt_dev_from_bdf("0000:03:00.0", &dev) < 0) {
///     // Handle error
/// }
/// tt_session_t sess;
/// tt_open(&dev, &sess, 0);
/// // ... use session ...
/// tt_close(&sess);
/// ```
[[nodiscard]] int tt_dev_from_bdf(const char *addr, tt_device_t *dev);

/// Discover connected devices.
///
/// Scans for connected devices. Fills buffer with up to `cap` descriptors.
/// Return value may exceed `cap` if more devices were found. A missing
/// device directory means the driver is not loaded, so it scans as zero
/// devices rather than failing.
///
/// Must call `tt_open()` to obtain a session before using a device obtained
/// this way.
///
/// @param cap       Capacity of output buffer.
/// @param[out] buf  Buffer for found devices.
/// @return          Number of devices found (may exceed `cap`), negative on
///                  error.
///
/// @par Errors
///
/// Codes propagate from the failing system call.
///
/// @par Example
///
/// ```c
/// tt_device_t devs[16];
/// ssize_t count = tt_dev_scan(16, devs);
/// if (count < 0)
///     return count;
///
/// size_t actual = (count < 16) ? count : 16;
/// for (size_t i = 0; i < actual; i++) {
///     tt_session_t sess;
///     tt_open(&devs[i], &sess, 0);
///     // ... use session ...
///     tt_close(&sess);
/// }
/// ```
[[nodiscard]] ssize_t tt_dev_scan(size_t cap, tt_device_t buf[static cap]);

/// Session open flags.
///
/// Flags for `tt_open()`. Each flag is a single bit.
typedef enum tt_open_flag {
    /// Exclusive access.
    ///
    /// Waits until no other client has the device open, then blocks all
    /// other opens for the session's lifetime. A blocking exclusive open
    /// can be starved by a steady stream of shared opens, so combine with
    /// `TT_OPEN_NONBLOCK` to fail fast.
    ///
    /// This requires `tt-kmd` 2.10 or later, which arbitrates exclusive
    /// access at open time. Older drivers silently ignore the flag.
    TT_OPEN_EXCL     = (1U << 0),
    /// Non-blocking open.
    ///
    /// Fails with `EAGAIN` instead of waiting: an exclusive open fails
    /// while any other client has the device open, and a shared open fails
    /// while another client holds the device exclusively.
    TT_OPEN_NONBLOCK = (1U << 1),
} tt_open_flag_t;

/// Open a session handle for a device.
///
/// Opens the underlying device and initializes the session handle for use.
/// The open blocks while another client holds the device exclusively (e.g.
/// during `tt_reset()` or a flash sequence). The session struct is reusable:
/// it may be reopened after `tt_close()`.
///
/// @param dev       Device descriptor.
/// @param[out] sess Session handle to initialize.
/// @param flags     Bitmask of `tt_open_flag_t` values.
/// @return          0 on success, -1 on error (check `errno`).
///
/// @par Errors
///
/// * `EINVAL`     `dev` or `sess` is `NULL`, or `flags` contains unknown
///                bits.
/// * `ENODEV`     The device could not be opened.
/// * `EAGAIN`     `TT_OPEN_NONBLOCK` is set and another client holds the
///                device incompatibly.
[[nodiscard]] int
tt_open(const tt_device_t *dev, tt_session_t *sess, uint16_t flags);

/// Close a session handle.
///
/// Releases the file descriptor and invalidates the handle. After this call,
/// `sess->fd` is `-1`. Idempotent, so closing an already-closed session is a
/// no-op.
///
/// @param sess  Session handle.
/// @return      0 on success, -1 on error (check `errno`).
///
/// @par Errors
///
/// * `EINVAL`     `sess` is `NULL`.
///
/// Other codes propagate from the failing system call.
[[nodiscard]] int tt_close(tt_session_t *sess);

/// Reopen a session in place.
///
/// Closes the session's current descriptor, then reopens the same device with
/// the flags it was opened with. Use this to recover a session severed by an
/// out-of-band reset: the reopen succeeds after a reset and reports `ENODEV`
/// after a removal.
///
/// The reopen restores only the session handle. TLB allocations do not survive
/// it, and any requested power state is dropped with the old descriptor. The
/// stale descriptor is always released, so a failed reopen leaves the session
/// closed (`sess->fd` is `-1`).
///
/// @param sess  Session handle to reopen in place.
/// @return      0 on success, -1 on error (check `errno`).
///
/// @par Errors
///
/// * `EINVAL`     `sess` is `NULL`.
/// * `ENODEV`     The device could not be reopened.
/// * `EAGAIN`     The session was opened with `TT_OPEN_NONBLOCK` and another
///                client holds the device incompatibly.
[[nodiscard]] int tt_reopen(tt_session_t *sess);

/// Device information.
///
/// Contains a snapshot of static information about a device.
typedef struct tt_dev_info {
    /// Size of output structure.
    uint32_t output_size_bytes;
    /// PCI vendor ID.
    uint16_t vendor_id;
    /// PCI device ID.
    uint16_t device_id;
    /// PCI subsystem vendor ID.
    uint16_t subsystem_vendor_id;
    /// PCI subsystem ID.
    uint16_t subsystem_id;
    /// PCI bus/device/function (BDF).
    uint16_t bus_dev_fn;
    /// Max direct memory access (DMA) buffer size (log2).
    uint16_t max_dma_buf_size_log2;
    /// PCI domain.
    uint16_t pci_domain;
} tt_dev_info_t;

/// Fetch device information.
///
/// Gets static information about a device. Guaranteed not to change during the
/// device lifecycle.
///
/// @param sess      Session handle.
/// @param[out] info Device information output.
/// @return          0 on success, -1 on error (check `errno`).
///
/// @par Errors
///
/// * `EINVAL`     `sess` or `info` is `NULL`.
/// * `ENOTCONN`   The session is not open.
/// * `ECONNRESET` The session was severed by an out-of-band reset or removal.
///
/// Other codes propagate from the failing system call.
[[nodiscard]] int tt_dev_info(const tt_session_t *sess, tt_dev_info_t *info);

/*============================================================================*
 * ADDRESSING                                                                 *
 *============================================================================*/

/*
 * Translation Lookaside Buffer (TLB) windows provide direct memory-mapped
 * access to device network-on-chip (NOC) addresses. These are fixed-size
 * apertures that transparently translate host memory operations to device
 * transactions.
 */

/// TLB size.
///
/// Valid TLB window sizes. Not all sizes are available on all architectures;
/// kernel validates availability.
typedef enum tt_tlb_size {
    /// 1MB window.
    ///
    /// Supported:
    /// - Wormhole
    TT_TLB_1MB  = (1UL << 20),
    /// 2MB window.
    ///
    /// Supported:
    /// - Wormhole
    /// - Blackhole
    TT_TLB_2MB  = (1UL << 21),
    /// 16MB window.
    ///
    /// Supported:
    /// - Wormhole
    TT_TLB_16MB = (1UL << 24),
    /// 4GB window.
    ///
    /// Supported:
    /// - Blackhole
    TT_TLB_4GB  = (1UL << 32),
} tt_tlb_size_t;

/// TLB handle.
///
/// Contains TLB identifier, mapped pointer, and size. Allocated by
/// `tt_tlb_alloc()` and freed by `tt_tlb_free()`. The `ptr` field is `NULL`
/// until `tt_tlb_bind()` is called.
typedef struct tt_tlb {
    /// TLB identifier.
    uint32_t id;
    /// Memory-mapped window (`NULL` until configured).
    void *ptr;
    /// Window size in bytes.
    size_t len;
    /// Memory-map offset (for internal use).
    uint64_t idx;
} tt_tlb_t;

/// TLB cache mode.
typedef enum tt_tlb_cache_mode {
    /// Uncached.
    ///
    /// Use for register access where ordering and immediate visibility matter.
    TT_TLB_UC = 0,
    /// Write-combined.
    ///
    /// Use for memory access where batching writes improves performance.
    TT_TLB_WC = 1,
} tt_tlb_cache_mode_t;

/// TLB NOC configuration.
///
/// Specifies the NOC target and address mapping for a TLB window.
typedef struct tt_tlb_config {
    /// Device address.
    uint64_t addr;
    /// Target X coordinate.
    uint8_t x_end;
    /// Target Y coordinate.
    uint8_t y_end;
    /// Multicast start X.
    uint8_t x_start;
    /// Multicast start Y.
    uint8_t y_start;
    /// NOC selector (`0` or `1`).
    uint8_t noc;
    /// Multicast enable.
    bool mcast;
    /// Linked TLB flag.
    bool linked;
    /// Static virtual channel.
    uint8_t static_vc;
} tt_tlb_config_t;

/// Allocate a TLB window.
///
/// Allocates a TLB of the requested size from the device. The kernel validates
/// size availability for the device architecture. The returned TLB will not yet
/// have a pointer (`NULL`) and must be bound with `tt_tlb_bind()` before use.
///
/// On an invalid `mode`, the freshly allocated TLB is freed before the call
/// returns `EINVAL`.
///
/// @param sess     Session handle.
/// @param size     Window size.
/// @param mode     Cache mode.
/// @param[out] tlb Allocated TLB handle with `ptr = NULL`.
/// @return         0 on success, -1 on error (check `errno`).
///
/// @par Errors
///
/// * `EINVAL`     `sess` or `tlb` is `NULL`, or `mode` is invalid.
/// * `ENOTCONN`   The session is not open.
/// * `ECONNRESET` The session was severed by an out-of-band reset or removal.
///
/// Other codes propagate from the failing system call.
[[nodiscard]] int tt_tlb_alloc(
    const tt_session_t *sess,
    tt_tlb_size_t size,
    tt_tlb_cache_mode_t mode,
    tt_tlb_t *tlb
);

/// Bind a TLB to a NOC address.
///
/// Maps the TLB window into the process address space and points it at the
/// given NOC target address and coordinates. "Bind" reflects that this
/// operation associates (binds) the window to a specific device address,
/// analogous to binding a socket to a network address.
///
/// Calling with an already-mapped TLB to rebind will invalidate stale interior
/// pointers (fail-fast on misuse).
///
/// On failure the window is left completely unconfigured (`ptr` is `NULL`),
/// including the previous mapping on a failed rebind.
///
/// @param sess  Session handle.
/// @param tlb   TLB handle.
/// @param cfg   NOC configuration.
/// @return      0 on success, -1 on error (check `errno`).
///
/// @par Errors
///
/// * `EINVAL`     `sess`, `tlb`, or `cfg` is `NULL`.
/// * `ENOTCONN`   The session is not open.
/// * `ECONNRESET` The session was severed by an out-of-band reset or removal.
///
/// Other codes propagate from the failing system call.
[[nodiscard]] int tt_tlb_bind(
    const tt_session_t *sess, tt_tlb_t *tlb, const tt_tlb_config_t *cfg
);

/// Free a TLB window.
///
/// Releases the TLB window and unmaps its memory region. An unconfigured
/// window (never bound) needs no unmap. On success the handle's `id` is
/// cleared.
///
/// @param sess  Session handle.
/// @param tlb   TLB handle.
/// @return      0 on success, -1 on error (check `errno`).
///
/// @par Errors
///
/// * `EINVAL`     `sess` or `tlb` is `NULL`.
/// * `ENOTCONN`   The session is not open.
/// * `ECONNRESET` The session was severed by an out-of-band reset or removal.
///
/// Other codes propagate from the failing system call.
[[nodiscard]] int tt_tlb_free(const tt_session_t *sess, tt_tlb_t *tlb);

/*============================================================================*
 * MESSAGING                                                                  *
 *                                                                            *
 * The System Management Controller (SMC) is the embedded controller          *
 * managing firmware, power, and clocks.                                      *
 *============================================================================*/

/// Default response timeout in milliseconds.
///
/// Selected by passing `0` as a timeout.
#define TT_SMC_TIMEOUT_DEFAULT 1000u

/// SMC message.
typedef struct tt_smc_msg {
    /// Message words. Word 0 is the header: the message code in its low byte
    /// plus any per-message packed fields, and the firmware status on a
    /// response. Words 1 through 7 carry the request arguments or the
    /// response data.
    uint32_t message[8];
} tt_smc_msg_t;

/// Call SMC with a message.
///
/// Posts `req` to the controller's per-session message queue, then polls for
/// the response until it arrives or `timeout` milliseconds elapse, and writes
/// the reply to `rsp`. A session holds at most one message outstanding at a
/// time. An outstanding message is dropped on any early return, leaving the
/// session free to post again. A dropped message may still run, as described
/// for `tt_smc_drop()`.
///
/// The request is left untouched, so the same message can be sent again. One
/// object may serve as both `req` and `rsp` to exchange a message in place.
///
/// Success means the exchange completed, not that the firmware accepted the
/// message: inspect `rsp.message[0]` (the firmware status, `0` on success) for
/// message-level errors.
///
/// @param sess           Session handle.
/// @param req            SMC message to send.
/// @param[out] rsp       Response message.
/// @param timeout        Poll timeout in ms (`0` for `TT_SMC_TIMEOUT_DEFAULT`).
/// @return               0 on success, -1 on error (check `errno`).
///
/// @par Errors
///
/// * `EINVAL`     `sess`, `req`, or `rsp` is `NULL`.
/// * `ENOTCONN`   The session is not open.
/// * `EBUSY`      The session already has a message outstanding.
/// * `ETIMEDOUT`  No response arrived within `timeout`.
/// * `ENOTSUP`    The firmware provides no usable message queue.
/// * `ECONNRESET` The session was severed by an out-of-band reset or removal.
///
/// Other codes propagate from the failing system call.
[[nodiscard]] int tt_smc_call(
    const tt_session_t *sess,
    const tt_smc_msg_t *req,
    tt_smc_msg_t *rsp,
    uint32_t timeout
);

/// Post a message to SMC.
///
/// Submits `req` to the controller's per-session message queue and returns
/// without waiting for the response. The message stays outstanding until
/// `tt_smc_poll()` or `tt_smc_wait()` retrieves the response, or
/// `tt_smc_drop()` discards it. A session holds at most one message
/// outstanding at a time.
///
/// The request is left untouched, so the same message can be posted again once
/// the response is retrieved.
///
/// @param sess  Session handle.
/// @param req   SMC message to post.
/// @return      0 on success, -1 on error (check `errno`).
///
/// @par Errors
///
/// * `EINVAL`     `sess` or `req` is `NULL`.
/// * `ENOTCONN`   The session is not open.
/// * `EBUSY`      The session already has a message outstanding.
/// * `ENOTSUP`    The firmware provides no usable message queue.
/// * `ECONNRESET` The session was severed by an out-of-band reset or removal.
///
/// Other codes propagate from the failing system call.
[[nodiscard]] int
tt_smc_post(const tt_session_t *sess, const tt_smc_msg_t *req);

/// Poll for a response from SMC.
///
/// Checks once whether the response to a posted message is ready, without
/// blocking. On success writes the reply to `rsp`, freeing the session to post
/// again. A response that is not ready leaves the message outstanding, to poll
/// again or cancel, and `rsp` untouched.
///
/// Success means the exchange completed, not that the firmware accepted the
/// message: inspect `rsp.message[0]` (the firmware status, `0` on success) for
/// message-level errors. A failed exchange reports its own error and consumes
/// the message, also freeing the session to post again.
///
/// @param sess           Session handle.
/// @param[out] rsp       Response message.
/// @return               0 on success, -1 on error (check `errno`).
///
/// @par Errors
///
/// * `EINVAL`     `sess` or `rsp` is `NULL`.
/// * `ENOTCONN`   The session is not open.
/// * `EAGAIN`     No response is ready yet.
/// * `ESRCH`      The session has no message outstanding.
/// * `ECONNRESET` The session was severed by an out-of-band reset or removal.
///
/// Other codes propagate from the failing exchange.
[[nodiscard]] int tt_smc_poll(const tt_session_t *sess, tt_smc_msg_t *rsp);

/// Wait for a response from SMC.
///
/// Polls for the response to a posted message until it arrives, `timeout`
/// milliseconds elapse, or the exchange fails. On success writes the reply to
/// `rsp`, freeing the session to post again. On timeout the message is left
/// outstanding, to wait on again or cancel, and `rsp` untouched.
///
/// Success means the exchange completed, not that the firmware accepted the
/// message: inspect `rsp.message[0]` (the firmware status, `0` on success) for
/// message-level errors. A failed exchange reports its own error and consumes
/// the message, also freeing the session to post again.
///
/// @param sess           Session handle.
/// @param[out] rsp       Response message.
/// @param timeout        Poll timeout in ms (`0` for `TT_SMC_TIMEOUT_DEFAULT`).
/// @return               0 on success, -1 on error (check `errno`).
///
/// @par Errors
///
/// * `EINVAL`     `sess` or `rsp` is `NULL`.
/// * `ENOTCONN`   The session is not open.
/// * `ESRCH`      The session has no message outstanding.
/// * `ETIMEDOUT`  No response arrived within `timeout`.
/// * `ECONNRESET` The session was severed by an out-of-band reset or removal.
///
/// Other codes propagate from the failing exchange.
[[nodiscard]] int
tt_smc_wait(const tt_session_t *sess, tt_smc_msg_t *rsp, uint32_t timeout);

/// Drop an outstanding SMC message.
///
/// Discards the message posted with `tt_smc_post()`. A session with
/// nothing outstanding is left untouched. The session is free to post again
/// on return.
///
/// Dropping does not reliably stop the message. The driver discards it
/// outright only while it is still queued behind another client's message.
/// Once it reaches the controller, which is usually before
/// `tt_smc_post()` returns, the message runs and only its response is
/// thrown away. A caller cannot tell the two apart, so treat a dropped
/// message as one that may have run and may have changed device state.
///
/// @param sess  Session handle.
/// @return      0 on success, -1 on error (check `errno`).
///
/// @par Errors
///
/// * `EINVAL`     `sess` is `NULL`.
/// * `ENOTCONN`   The session is not open.
/// * `ECONNRESET` The session was severed by an out-of-band reset or removal.
///
/// Other codes propagate from the failing system call.
[[nodiscard]] int tt_smc_drop(const tt_session_t *sess);

/*============================================================================*
 * TELEMETRY                                                                  *
 *                                                                            *
 * Raw telemetry data read from device.                                       *
 *============================================================================*/

/// Telemetry tags.
typedef enum tt_telemetry_tag {
    /// Invalid/reserved tag (index 0 is unused).
    TT_TAG_INVALID              = 0,
    /// High part of the board ID.
    TT_TAG_BOARD_ID_HIGH        = 1,
    /// Low part of the board ID.
    TT_TAG_BOARD_ID_LOW         = 2,
    /// ASIC ID.
    TT_TAG_ASIC_ID              = 3,
    /// Harvesting state of the system.
    TT_TAG_HARVESTING_STATE     = 4,
    /// Update interval for telemetry in milliseconds.
    TT_TAG_UPDATE_TELEM_SPEED   = 5,
    /// VCore voltage in millivolts.
    TT_TAG_VCORE                = 6,
    /// Thermal design power (TDP) in watts.
    TT_TAG_TDP                  = 7,
    /// Thermal design current (TDC) in amperes.
    TT_TAG_TDC                  = 8,
    /// VDD limits (min and max) in millivolts.
    TT_TAG_VDD_LIMITS           = 9,
    /// Thermal shutdown limit in degrees Celsius.
    TT_TAG_THM_LIMIT_SHUTDOWN   = 10,
    /// ASIC temperature in signed 16.16 fixed-point format.
    TT_TAG_ASIC_TEMPERATURE     = 11,
    /// Voltage regulator temperature in degrees Celsius (not implemented).
    TT_TAG_VREG_TEMPERATURE     = 12,
    /// Board temperature in degrees Celsius (not implemented).
    TT_TAG_BOARD_TEMPERATURE    = 13,
    /// AI clock frequency in megahertz.
    TT_TAG_AICLK                = 14,
    /// AXI clock frequency in megahertz.
    TT_TAG_AXICLK               = 15,
    /// SMC clock frequency in megahertz.
    TT_TAG_SMCCLK               = 16,
    /// L2CPU clock 0 frequency in megahertz.
    TT_TAG_L2CPUCLK0            = 17,
    /// L2CPU clock 1 frequency in megahertz.
    TT_TAG_L2CPUCLK1            = 18,
    /// L2CPU clock 2 frequency in megahertz.
    TT_TAG_L2CPUCLK2            = 19,
    /// L2CPU clock 3 frequency in megahertz.
    TT_TAG_L2CPUCLK3            = 20,
    /// Ethernet live status.
    TT_TAG_ETH_LIVE_STATUS      = 21,
    /// GDDR status.
    TT_TAG_GDDR_STATUS          = 22,
    /// GDDR speed in megabits per second.
    TT_TAG_GDDR_SPEED           = 23,
    /// Ethernet firmware version.
    TT_TAG_ETH_FW_VERSION       = 24,
    /// GDDR firmware version.
    TT_TAG_GDDR_FW_VERSION      = 25,
    /// DM application firmware version.
    TT_TAG_DM_APP_FW_VERSION    = 26,
    /// DM bootloader firmware version.
    TT_TAG_DM_BL_FW_VERSION     = 27,
    /// Flash bundle version.
    TT_TAG_FLASH_BUNDLE_VERSION = 28,
    /// CM firmware version.
    TT_TAG_CM_FW_VERSION        = 29,
    /// L2CPU firmware version.
    TT_TAG_L2CPU_FW_VERSION     = 30,
    /// Fan speed as a percentage.
    TT_TAG_FAN_SPEED            = 31,
    /// Timer heartbeat counter.
    TT_TAG_TIMER_HEARTBEAT      = 32,
    /// Total number of telemetry tags.
    TT_TAG_TELEM_ENUM_COUNT     = 33,
    /// Enabled Tensix columns.
    TT_TAG_ENABLED_TENSIX_COL   = 34,
    /// Enabled Ethernet interfaces.
    TT_TAG_ENABLED_ETH          = 35,
    /// Enabled GDDR interfaces.
    TT_TAG_ENABLED_GDDR         = 36,
    /// Enabled L2CPU cores.
    TT_TAG_ENABLED_L2CPU        = 37,
    /// PCIe usage information.
    TT_TAG_PCIE_USAGE           = 38,
    /// Input current in amperes.
    TT_TAG_INPUT_CURRENT        = 39,
    /// NOC translation status.
    TT_TAG_NOC_TRANSLATION      = 40,
    /// Fan RPM.
    TT_TAG_FAN_RPM              = 41,
    /// GDDR 0 and 1 temperature.
    TT_TAG_GDDR_0_1_TEMP        = 42,
    /// GDDR 2 and 3 temperature.
    TT_TAG_GDDR_2_3_TEMP        = 43,
    /// GDDR 4 and 5 temperature.
    TT_TAG_GDDR_4_5_TEMP        = 44,
    /// GDDR 6 and 7 temperature.
    TT_TAG_GDDR_6_7_TEMP        = 45,
    /// GDDR 0 and 1 corrected errors.
    TT_TAG_GDDR_0_1_CORR_ERRS   = 46,
    /// GDDR 2 and 3 corrected errors.
    TT_TAG_GDDR_2_3_CORR_ERRS   = 47,
    /// GDDR 4 and 5 corrected errors.
    TT_TAG_GDDR_4_5_CORR_ERRS   = 48,
    /// GDDR 6 and 7 corrected errors.
    TT_TAG_GDDR_6_7_CORR_ERRS   = 49,
    /// GDDR uncorrected errors.
    TT_TAG_GDDR_UNCORR_ERRS     = 50,
    /// Maximum GDDR temperature.
    TT_TAG_MAX_GDDR_TEMP        = 51,
    /// ASIC location.
    TT_TAG_ASIC_LOCATION        = 52,
    /// Board power limit in watts.
    TT_TAG_BOARD_POWER_LIMIT    = 53,
    /// Input power in watts.
    TT_TAG_INPUT_POWER          = 54,
    /// Maximum TDC limit in amperes.
    TT_TAG_TDC_LIMIT_MAX        = 55,
    /// Thermal throttle limit in degrees Celsius.
    TT_TAG_THM_LIMIT_THROTTLE   = 56,
    /// Firmware build date.
    TT_TAG_FW_BUILD_DATE        = 57,
    /// TT flash version.
    TT_TAG_TT_FLASH_VERSION     = 58,
    /// Enabled Tensix rows.
    TT_TAG_ENABLED_TENSIX_ROW   = 59,
    /// Thermal trip count.
    TT_TAG_THERM_TRIP_COUNT     = 60,
    /// High part of the ASIC ID.
    TT_TAG_ASIC_ID_HIGH         = 61,
    /// Low part of the ASIC ID.
    TT_TAG_ASIC_ID_LOW          = 62,
    /// Maximum AI clock frequency.
    TT_TAG_AICLK_LIMIT_MAX      = 63,
    /// Maximum TDP limit in watts.
    TT_TAG_TDP_LIMIT_MAX        = 64,
    /// Effective minimum AICLK arbiter value in megahertz.
    ///
    /// This represents the highest frequency requested by all enabled
    /// minimum arbiters. Multiple arbiters may request minimum
    /// frequencies, and the highest value is effective.
    TT_TAG_AICLK_ARB_MIN        = 65,
    /// Effective maximum AICLK arbiter value in megahertz.
    ///
    /// This represents the lowest frequency limit imposed by all
    /// enabled maximum arbiters. Multiple arbiters may impose maximum
    /// frequency limits (e.g., TDP, TDC, thermal throttling), and the
    /// lowest (most restrictive) value is effective. This value takes
    /// precedence over TT_TAG_AICLK_ARB_MIN when determining the
    /// final target frequency.
    TT_TAG_AICLK_ARB_MAX        = 66,
    /// Bitmask of enabled minimum arbiters.
    ///
    /// Each bit represents whether a specific minimum frequency arbiter is
    /// currently enabled.
    TT_TAG_ENABLED_MIN_ARB      = 67,
    /// Bitmask of enabled maximum arbiters.
    ///
    /// Each bit represents whether a specific maximum frequency arbiter is
    /// currently enabled.
    TT_TAG_ENABLED_MAX_ARB      = 68,
    /// Sentinel value for telemetry array length.
    TT_TELEMETRY_LEN,
} tt_telemetry_tag_t;

/// Telemetry data.
///
/// Array indexed by `tt_telemetry_tag_t`.
typedef uint32_t tt_telemetry_t[TT_TELEMETRY_LEN];

/// Read telemetry from device.
///
/// Returns a complete, coherent snapshot rather than a partial read. The
/// table is zeroed before filling, and tags the firmware does not report
/// read as zero. Values are verified against the firmware heartbeat, with
/// up to three retries on a mismatch.
///
/// @param sess         Session handle.
/// @param[out] table   Telemetry data output.
/// @return             0 on success, -1 on error (check `errno`).
///
/// @par Errors
///
/// * `EINVAL`     `sess` or `table` is `NULL`.
/// * `ENOTCONN`   The session is not open.
/// * `ECONNRESET` The session was severed by an out-of-band reset or removal.
/// * `EIO`        Telemetry data was malformed or could not be read.
/// * `ENOTSUP`    The device architecture is unsupported.
///
/// Other codes propagate from the failing system call.
[[nodiscard]] int tt_telemetry(const tt_session_t *sess, tt_telemetry_t table);

/*============================================================================*
 * POWER                                                                      *
 *                                                                            *
 * Power state management. The kernel driver aggregates power requests from   *
 * all connected clients. Each client's contribution is removed when its file *
 * descriptor is closed.                                                      *
 *============================================================================*/

/// Power feature flags.
///
/// Individual power feature controls passed to `tt_power()`.
/// Each flag is a single bit. Set it to enable the feature, clear to disable.
///
/// @note Sessions opened via `tt_open()` start with all features off.
/// Use `tt_power()` to request power features explicitly.
typedef enum tt_power_flag {
    /// AI clock selection.
    ///
    /// Requests maximum AI clock frequency when set; minimum when clear.
    TT_POWER_MAX_AI_CLK       = (1U << 0),
    /// GDDR PHY state.
    ///
    /// Wakes up the GDDR PHY when set. Powers it down when clear.
    TT_POWER_MRISC_PHY_WAKEUP = (1U << 1),
    /// Tensix core gating.
    ///
    /// Enables Tensix cores when set. Clock gates them when clear.
    TT_POWER_TENSIX_ENABLE    = (1U << 2),
    /// L2CPU clock gating.
    ///
    /// Enables L2CPU when set. Clock gates it when clear.
    TT_POWER_L2CPU_ENABLE     = (1U << 3),
} tt_power_flag_t;

/// Request device power features.
///
/// Requests the given feature flags from the firmware. Any flag not set in
/// `flags` is considered requested off. The effective state may differ from the
/// requested state because other open file descriptors may have requested
/// different features.
///
/// The kernel driver aggregates requests across all open file descriptors for
/// the device: a feature is on if any client requests it. Each client's
/// contribution is removed when its file descriptor is closed. The library does
/// not track what was last requested. Callers that need to remember previous
/// state must do so themselves.
///
/// @par Example
///
/// ```c
/// uint16_t flags = TT_POWER_MAX_AI_CLK | TT_POWER_TENSIX_ENABLE;
/// if (tt_power(&sess, flags) < 0) {
///     // Handle error
/// }
/// ```
///
/// @param sess   Session handle.
/// @param flags  Bitmask of `tt_power_flag_t` values to enable.
/// @return       0 on success, -1 on error (check `errno`).
///
/// @par Errors
///
/// * `EINVAL`     `sess` is `NULL`.
/// * `ENOTCONN`   The session is not open.
/// * `ECONNRESET` The session was severed by an out-of-band reset or removal.
///
/// Other codes propagate from the failing system call.
[[nodiscard]] int tt_power(const tt_session_t *sess, uint16_t flags);

/*============================================================================*
 * RESET                                                                      *
 *                                                                            *
 * Device reset operations.                                                   *
 *============================================================================*/

/// Trigger device reset.
///
/// Acquires exclusive access to the device, issues the full reset sequence
/// (ASIC reset, completion wait, post-reset), and releases it. The caller
/// does not need an open session.
///
/// Acquisition succeeds only when no other client has the device open,
/// including descriptors held by the calling process. If the device is
/// busy, the reset fails with `EAGAIN` rather than resetting under other
/// clients or blocking indefinitely. A reset never destroys another
/// client's session out from under it. To reset a device the caller has
/// open, use `tt_reset_with()`.
///
/// The reset runs in place, so the device number does not change.
///
/// This requires `tt-kmd` 2.10 or later, which arbitrates exclusive access
/// at open time. The requirement is not checked at runtime.
///
/// @param dev   Device descriptor.
/// @return      0 on success, -1 on error (check `errno`).
///
/// @par Errors
///
/// * `EINVAL`     `dev` is `NULL`.
/// * `ENODEV`     The device does not exist.
/// * `EAGAIN`     Another client holds the device open.
/// * `ECONNRESET` The device was reset or removed out-of-band.
/// * `EIO`        The reset sequence failed.
/// * `ETIMEDOUT`  The reset did not complete in time.
///
/// Other codes propagate from the failing system call.
[[nodiscard]] int tt_reset(const tt_device_t *dev);

/// Trigger device reset via an open session.
///
/// Closes the session, then resets the underlying device as `tt_reset()`
/// does. The session is consumed on all paths, success or failure, just as
/// if `tt_close()` had been called. `sess->fd` is `-1` on return, and the
/// device number does not change, so `sess->dev` stays valid for reopening.
///
/// Exclusive acquisition requires the device to be idle, so the session is
/// closed before the reset begins. Another client may open the device in
/// that window, in which case the reset fails with `EAGAIN`.
///
/// @param sess  Session handle (consumed).
/// @return      0 on success, -1 on error (check `errno`).
///
/// @par Errors
///
/// * `EINVAL`     `sess` is `NULL`.
/// * `ENOTCONN`   The session is not open.
/// * `ENODEV`     The device does not exist.
/// * `EAGAIN`     Another client holds the device open.
/// * `ECONNRESET` The device was reset or removed out-of-band.
/// * `EIO`        The reset sequence failed.
/// * `ETIMEDOUT`  The reset did not complete in time.
///
/// Other codes propagate from the failing system call.
///
/// @par Example
///
/// ```c
/// tt_session_t sess;
/// if (tt_open(&dev, &sess, 0) < 0)
///     return -1;
/// if (tt_reset_with(&sess) < 0)
///     return -1;  // Session already consumed
/// tt_open(&sess.dev, &sess, 0);
/// // ... use fresh session ...
/// tt_close(&sess);
/// ```
[[nodiscard]] int tt_reset_with(tt_session_t *sess);

#ifdef __cplusplus
}
#endif

#endif /* TT_DAL_H */
