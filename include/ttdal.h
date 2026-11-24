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
 * @version 0.2.0
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
#define TTDAL_VERSION_MINOR 2
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
/// The function is defined in this header, so it reports the version of the
/// header the caller compiled against. It does not query the library the caller
/// links against.
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
/// The kernel-mode driver (KMD) version is a property of the kernel module, not
/// a specific device. This function reads the version the kernel publishes for
/// the loaded module, so it opens no device, and succeeds while every device is
/// held or none is connected. A pre-release suffix, such as `-rc1`, fills the
/// pre-release identifier.
///
/// @param[out] version  KMD version output.
/// @return              0 on success, -1 on error (check `errno`).
///
/// @par Errors
///
/// * `EINVAL`     `version` is `NULL`.
/// * `ENODEV`     The KMD is not loaded.
/// * `EIO`        The version string was empty or malformed.
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
 * * `EPERM`      Session lacks the exclusive access an unforced reset
 *                requires.
 * * `ENODEV`     No such device (normalized from device lookups, and from an
 *                open that finds no device).
 * * `EAGAIN`     Device held incompatibly (non-blocking open), or held
 *                exclusively by any client during a device lookup.
 * * `ECONNRESET` Device connection reset. The session was severed by an
 *                out-of-band reset or removal (normalized from `ENODEV` on
 *                session operations). Reopening recovers the session after a
 *                reset and reports `ENODEV` after a removal.
 * * `EIO`        Device I/O failed (also raised for driver
 *                soft-failures).
 * * `ETIMEDOUT`  Reset or SMC message exchange did not complete in time.
 * * `ENOTRECOVERABLE` Device did not reinitialize after a reset.
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
/// Obtain via `tt_dev_scan()`, `tt_dev_from_path()`, or `tt_dev_from_bdf()`. To
/// perform operations on the device, open a session handle with `tt_open()`.
typedef struct tt_device {
    /// Device identifier.
    ///
    /// Uniquely identifies a device. Suitable for comparisons, as multiple
    /// descriptors for the same device will share this identifier.
    ///
    /// TODO: This value should remain stable when the device is removed and
    /// re-probed, although the current implementation does not reflect this.
    /// This could be accomplished by using a driver-provided UUID rather than
    /// using the device number.
    ///
    /// NOTE: This choice of type is unstable, and may be changed without
    /// warning before the first stable release.
    uint32_t id;
} tt_device_t;

/// Open session handle.
///
/// An owned handle that holds an open file descriptor to a device. Required to
/// perform any operation that interacts with hardware.
///
/// Obtain via `tt_open()`, release via `tt_close()`. A handle is invalidated by
/// an out-of-band device reset or removal. After a reset, `tt_reopen()`
/// restores it in place.
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
    /// Bitmask of `tt_open_flag_t` values the session was opened with, reused
    /// by `tt_reopen()`.
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
/// * `EINVAL`     `path` or `dev` is `NULL`, or `path` is not a
///                `/dev/tenstorrent/` device node.
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
/// matching the given BDF. Accepts `DDDD:BB:DD.F` or `BB:DD.F` format. Each
/// scanned device is briefly opened to read and compare its BDF.
///
/// The opens do not wait for an exclusive holder. A device held exclusively
/// (e.g. during `tt_reset()` or a flash sequence) cannot be read, so it is
/// skipped. When no device matches and at least one was skipped, the call fails
/// with `EAGAIN`, since a skipped device may be the one sought. It is safe to
/// retry once the holder releases the device.
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
/// * `EAGAIN`     No device matches, and a device held exclusively, by another
///                client or by the caller, could not be checked.
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
/// Return value may exceed `cap` if more devices were found. A missing device
/// directory means the driver is not loaded, so it scans as zero devices rather
/// than failing.
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
    /// Waits until no other client has the device open, then blocks all other
    /// opens for the session's lifetime. A blocking exclusive open can be
    /// starved by a steady stream of shared opens, so combine with
    /// `TT_OPEN_NONBLOCK` to fail fast.
    ///
    /// This requires `tt-kmd` 2.10 or later, which arbitrates exclusive access
    /// at open time. Older drivers silently ignore the flag.
    TT_OPEN_EXCL     = (1U << 0),
    /// Non-blocking open.
    ///
    /// Fails with `EAGAIN` instead of waiting: an exclusive open fails while
    /// any other client has the device open, and a shared open fails while
    /// another client holds the device exclusively.
    TT_OPEN_NONBLOCK = (1U << 1),
} tt_open_flag_t;

/// Open a session handle for a device.
///
/// Opens the underlying device and initializes the session handle for use. The
/// open blocks while another client holds the device exclusively (e.g. during
/// `tt_reset()` or a flash sequence). The session struct is reusable: it may be
/// reopened after `tt_close()`.
///
/// @param dev       Device descriptor.
/// @param[out] sess Session handle to initialize.
/// @param flags     Bitmask of `tt_open_flag_t` values.
/// @return          0 on success, -1 on error (check `errno`).
///
/// @par Errors
///
/// * `EINVAL`     `dev` or `sess` is `NULL`, or `flags` contains unknown bits.
/// * `ENODEV`     The device does not exist.
/// * `EACCES`     The caller may not open the device node.
/// * `EAGAIN`     `TT_OPEN_NONBLOCK` is set and another client holds the device
///                incompatibly.
///
/// Other codes propagate from the failing system call.
[[nodiscard]] int
tt_open(const tt_device_t *dev, tt_session_t *sess, uint16_t flags);

/// Close a session handle.
///
/// Releases the file descriptor and invalidates the handle. The descriptor is
/// released even when the call reports an error, so after this call `sess->fd`
/// is `-1` either way. Idempotent, so closing an already-closed session is a
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
/// out-of-band reset. After a removal, the reopen reports `ENODEV` instead.
///
/// The reopen restores only the session handle. TLB allocations do not survive
/// it, and any requested power state is dropped with the old descriptor. Ensure
/// each TLB allocated through the session is freed first, as the new descriptor
/// may be handed the same TLB IDs, which a stale TLB would then free or rebind.
/// The stale descriptor is always released, so a failed reopen leaves the
/// session closed (`sess->fd` is `-1`).
///
/// @param sess  Session handle to reopen in place.
/// @return      0 on success, -1 on error (check `errno`).
///
/// @par Errors
///
/// * `EINVAL`     `sess` is `NULL`.
/// * `ENODEV`     The device does not exist.
/// * `EACCES`     The caller may not open the device node.
/// * `EAGAIN`     The session was opened with `TT_OPEN_NONBLOCK` and another
///                client holds the device incompatibly.
///
/// Other codes propagate from the failing system call.
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
/// Valid TLB window sizes. Not all sizes are available on all architectures.
/// The kernel rejects unsupported sizes at allocation time.
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
/// If the window cannot be mapped or configured, it is left completely
/// unconfigured (`ptr` is `NULL`), including the previous mapping on a failed
/// rebind.
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
/// Releases the TLB window and unmaps its memory region. An unconfigured window
/// (never bound) needs no unmap. On success the handle's `id` is cleared.
///
/// The window is unmapped even if the session is no longer open. A live mapping
/// keeps the closed descriptor alive in the driver, and with it the TLB and any
/// exclusive hold on the device.
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
 *                                                                            *
 * These calls need `tt-kmd` 2.11 or later, which is above the 2.10 floor the *
 * rest of the library requires. An older driver has no message ioctl and     *
 * rejects the request with `EINVAL`, though a device the driver considers    *
 * uninitialized fails earlier with `ECONNRESET`. See QUIRKS.md.              *
 *============================================================================*/

/// Default response timeout in milliseconds.
#define TT_SMC_TIMEOUT_DEFAULT 1000u

/// SMC message.
typedef struct tt_smc_msg {
    /// Message words. Word 0 is the header: the message code in its low byte
    /// plus any per-message packed fields, and the firmware status on a
    /// response. Words 1 through 7 carry the request arguments or the response
    /// data.
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
/// @param timeout        Poll timeout in ms, such as `TT_SMC_TIMEOUT_DEFAULT`.
///                       `0` polls once.
/// @return               0 on success, -1 on error (check `errno`).
///
/// @par Errors
///
/// * `EINVAL`     `sess`, `req`, or `rsp` is `NULL`, or the driver is older
///                than `tt-kmd` 2.11.
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
/// * `EINVAL`     `sess` or `req` is `NULL`, or the driver is older than
///                `tt-kmd` 2.11.
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
/// again or drop, and `rsp` untouched.
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
/// outstanding, to wait on again or drop, and `rsp` untouched.
///
/// Success means the exchange completed, not that the firmware accepted the
/// message: inspect `rsp.message[0]` (the firmware status, `0` on success) for
/// message-level errors. A failed exchange reports its own error and consumes
/// the message, also freeing the session to post again.
///
/// @param sess           Session handle.
/// @param[out] rsp       Response message.
/// @param timeout        Poll timeout in ms, such as `TT_SMC_TIMEOUT_DEFAULT`.
///                       `0` polls once.
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
/// Discards the message posted with `tt_smc_post()`. A session with nothing
/// outstanding is left untouched. The session is free to post again on return.
///
/// Dropping does not reliably stop the message. The driver discards it outright
/// only while it is still queued behind another client's message. Once it
/// reaches the controller, which is usually before `tt_smc_post()` returns, the
/// message runs and only its response is thrown away. A caller cannot tell the
/// two apart, so treat a dropped message as one that may have run and may have
/// changed device state.
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

/// Telemetry entry.
///
/// One entry of the firmware's telemetry table: a tag number and the value
/// published under it. The library neither names tags nor decodes values. The
/// meaning and encoding of each tag are defined by the firmware, documented in
/// [tt-system-firmware](https://github.com/tenstorrent/tt-system-firmware).
typedef struct tt_telemetry {
    /// Firmware tag number.
    uint16_t tag;
    /// Raw telemetry value.
    uint32_t raw;
} tt_telemetry_t;

/// Read telemetry from device.
///
/// Reads a complete, coherent snapshot of the firmware's telemetry table. Fills
/// buffer with up to `cap` entries, in the firmware's table order, skipping
/// empty slots (tag 0). Return value may exceed `cap` if the table holds more
/// entries. Each tag appears at most once, and tags the firmware does not
/// publish are absent. When the firmware publishes a heartbeat, values are
/// verified against it, with up to three retries on a mismatch.
///
/// @param sess      Session handle.
/// @param cap       Capacity of output buffer.
/// @param[out] buf  Buffer for telemetry entries.
/// @return          Number of entries in the table (may exceed `cap`), negative
///                  on error.
///
/// @par Errors
///
/// * `EINVAL`     `sess` is `NULL`.
/// * `ENOTCONN`   The session is not open.
/// * `ECONNRESET` The session was severed by an out-of-band reset or removal.
/// * `EIO`        Telemetry data was malformed or could not be read.
/// * `ENOTSUP`    The device architecture is unsupported.
///
/// Other codes propagate from the failing system call.
///
/// @par Example
///
/// ```c
/// tt_telemetry_t telem[128];
/// ssize_t count = tt_telemetry(&sess, 128, telem);
/// if (count < 0)
///     return count;
///
/// size_t actual = (count < 128) ? count : 128;
/// for (size_t i = 0; i < actual; i++)
///     printf("%u: %u\n", telem[i].tag, telem[i].raw);
/// ```
[[nodiscard]] ssize_t tt_telemetry(
    const tt_session_t *sess, size_t cap, tt_telemetry_t buf[static cap]
);

/*============================================================================*
 * POWER                                                                      *
 *                                                                            *
 * Power state management. The kernel driver aggregates power requests from   *
 * all connected clients. Each client's contribution is removed when its file *
 * descriptor is closed.                                                      *
 *============================================================================*/

/// Power feature flags.
///
/// Individual power feature controls passed to `tt_power()`. Each flag is a
/// single bit. Set it to enable the feature, clear to disable.
///
/// @note Sessions opened via `tt_open()` start with all features off. Use
/// `tt_power()` to request power features explicitly.
typedef enum tt_power_flag {
    /// AI clock selection.
    ///
    /// Requests maximum AI clock frequency when set, and minimum when clear.
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

/// Device reset flags.
///
/// Flags for `tt_reset()` and `tt_reset_with()`. Each flag is a single bit.
typedef enum tt_reset_flag {
    /// Forced reset.
    ///
    /// Resets even if other clients hold the device. Their sessions are
    /// invalidated and report `ECONNRESET` until reopened. A client holding
    /// the device with `TT_OPEN_EXCL` still keeps it, since no other client
    /// can open the device while it is held exclusively.
    TT_RESET_FORCE = (1U << 0),
} tt_reset_flag_t;

/// Trigger device reset.
///
/// Acquires exclusive access to the device, issues the full reset sequence
/// (ASIC reset, completion wait, post-reset), and releases it. The caller
/// does not need an open session.
///
/// Acquisition succeeds only when no other client has the device open,
/// including descriptors held by the calling process. If the device is
/// busy, the reset fails with `EAGAIN` rather than resetting under other
/// clients or blocking indefinitely. Unless forced, a reset never destroys
/// another client's session out from under it. To reset a device the caller
/// has open, use `tt_reset_with()`.
///
/// With `TT_RESET_FORCE`, the device is opened shared instead, so the reset
/// goes ahead while other clients hold it and invalidates their sessions.
/// The open still does not wait, and it fails with `EAGAIN` while another
/// client holds the device with `TT_OPEN_EXCL`.
///
/// The reset runs in place, so the device number does not change.
///
/// This requires `tt-kmd` 2.10 or later, which arbitrates exclusive access
/// at open time. The requirement is not checked at runtime.
///
/// @param dev    Device descriptor.
/// @param flags  Bitmask of `tt_reset_flag_t` values.
/// @return       0 on success, -1 on error (check `errno`).
///
/// @par Errors
///
/// * `EINVAL`          `dev` is `NULL`, or `flags` contains unknown bits.
/// * `ENODEV`          The device does not exist.
/// * `EACCES`          The caller may not open the device node.
/// * `EAGAIN`          Another client holds the device open, or holds it
///                     with `TT_OPEN_EXCL` when `TT_RESET_FORCE` is set.
/// * `ECONNRESET`      The device was reset or removed out-of-band.
/// * `EBUSY`           A TLB window on the device is exported as a dma-buf.
/// * `EIO`             The ASIC reset trigger failed.
/// * `ETIMEDOUT`       The reset did not complete in time.
/// * `ENOTRECOVERABLE` The device did not reinitialize after the reset.
///
/// A failure with `EIO`, `ETIMEDOUT`, or `ENOTRECOVERABLE` does not leave the
/// device as it was. After `EIO` or `ETIMEDOUT` the device accepts only info
/// queries and further resets until one completes. Retry the reset to recover.
/// After `ENOTRECOVERABLE` the device's state is unknown, and only a later
/// successful reset makes it usable again.
///
/// While the device awaits a completed reset, every other driver request on any
/// session reports `ECONNRESET`, even on a session opened afterwards, so
/// reopening does not recover it.
///
/// Other codes propagate from the failing system call.
[[nodiscard]] int tt_reset(const tt_device_t *dev, uint16_t flags);

/// Trigger device reset via an open session.
///
/// Issues the full reset sequence on the session's own descriptor. The
/// driver carries that descriptor across the reset, so the session stays
/// open and usable once the reset completes. The reset runs in place, so
/// the device number does not change.
///
/// The session stays open when the reset fails too, and the caller still closes
/// it. Windows mapped through the session do not survive the reset, so an
/// access through one faults with `SIGBUS`. Their TLBs must still be freed.
///
/// The session must have been opened with `TT_OPEN_EXCL` unless
/// `TT_RESET_FORCE` is set. Exclusivity can only be acquired at open time,
/// and an exclusive session is already the sole client, so the reset never
/// destroys another client's session out from under it. A shared session is
/// refused with `EPERM`. With `TT_RESET_FORCE` it resets under any other
/// client instead, whose sessions the driver then invalidates. Use
/// `tt_reset()` to reset a device the caller does not already hold.
///
/// This requires `tt-kmd` 2.10 or later, which carries the resetting
/// descriptor across the reset. The requirement is not checked at runtime.
///
/// @param sess   Session handle.
/// @param flags  Bitmask of `tt_reset_flag_t` values.
/// @return       0 on success, -1 on error (check `errno`).
///
/// @par Errors
///
/// * `EINVAL`          `sess` is `NULL`, or `flags` contains unknown bits.
/// * `ENOTCONN`        The session is not open.
/// * `EPERM`           The session was not opened with `TT_OPEN_EXCL`, and
///                     `TT_RESET_FORCE` is not set.
/// * `ECONNRESET`      The device was reset or removed out-of-band.
/// * `EBUSY`           A TLB window on the device is exported as a dma-buf.
/// * `EIO`             The ASIC reset trigger failed.
/// * `ETIMEDOUT`       The reset did not complete in time.
/// * `ENOTRECOVERABLE` The device did not reinitialize after the reset.
///
/// A failure with `EIO`, `ETIMEDOUT`, or `ENOTRECOVERABLE` does not leave the
/// device as it was, and the driver has already invalidated every other
/// client's descriptors. After `EIO` or `ETIMEDOUT` the device accepts only
/// info queries and further resets until one completes. Retry the reset to
/// recover. After `ENOTRECOVERABLE` the device's state is unknown, and only a
/// later successful reset makes it usable again.
///
/// While the device awaits a completed reset, every other driver request on any
/// session reports `ECONNRESET`, even on a session opened afterwards, so
/// reopening does not recover it.
///
/// Other codes propagate from the failing system call.
///
/// @par Example
///
/// ```c
/// tt_session_t sess;
/// if (tt_open(&dev, &sess, TT_OPEN_EXCL) < 0)
///     return -1;
/// if (tt_reset_with(&sess, 0) < 0) {
///     (void)tt_close(&sess);
///     return -1;
/// }
/// // ... session is still open ...
/// tt_close(&sess);
/// ```
[[nodiscard]] int tt_reset_with(const tt_session_t *sess, uint16_t flags);

#ifdef __cplusplus
}
#endif

#endif /* TT_DAL_H */
