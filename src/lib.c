// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

#include "ttdal.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "ioctl.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

// Thread-local errno for thread safety
_Thread_local tt_error_t tt_errno = TT_OK;

/*============================================================================*
 * VERSION                                                                    *
 *============================================================================*/

/// Get KMD version.
///
/// Discovers the first available device, issues the driver info `ioctl` through
/// it, then closes it.
int tt_version_driver(tt_version_t *version) {
    // Validate args
    if (!version)
        return tt_errno = TT_EINVAL, TT_ERR;

    // Use first available device.
    //
    // Driver version is global to the kernel module, so any open `fd`
    // suffices to issue the query.
    tt_device_t dev;
    if (tt_dev_scan(1, &dev) <= 0)
        return tt_errno = TT_ENODEV, TT_ERR;
    if (tt_dev_open(&dev) < 0)
        return TT_ERR;

    // Query driver info
    struct tenstorrent_get_driver_info query = {
        .in.output_size_bytes = sizeof(query.out),
    };
    int res = ioctl(dev.fd, TENSTORRENT_IOCTL_GET_DRIVER_INFO, &query);
    tt_dev_close(&dev);
    if (res != 0)
        return tt_errno = TT_EIO, TT_ERR;

    // Fill output
    *version = (tt_version_t){
        .major = query.out.driver_version_major,
        .minor = query.out.driver_version_minor,
        .patch = query.out.driver_version_patch,
    };

    return TT_OK;
}

/// Get firmware version.
///
/// Reads the version from a per-device attribute and parses it into a
/// `tt_version_t`.
int tt_version_firmware(const tt_device_t *dev, tt_version_t *version) {
    // Validate args
    if (!dev || !version)
        return tt_errno = TT_EINVAL, TT_ERR;

    // Ensure device is open
    if (dev->fd < 0)
        return tt_errno = TT_ENOTOPEN, TT_ERR;

    // Build sysfs path
    char path[64];
    snprintf(
        path,
        sizeof(path),
        "/sys/class/tenstorrent/tenstorrent!%u/tt_fw_bundle_ver",
        dev->id
    );

    // Read version string
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return tt_errno = TT_EIO, TT_ERR;

    char buf[32];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0)
        return tt_errno = TT_EIO, TT_ERR;
    buf[n] = '\0';

    // Parse "major.minor.patch.rc"
    uint64_t major, minor, patch, rc;
    if (sscanf(
            buf,
            "%" SCNu64 ".%" SCNu64 ".%" SCNu64 ".%" SCNu64,
            &major,
            &minor,
            &patch,
            &rc
        ) != 4)
        return tt_errno = TT_EIO, TT_ERR;

    *version = (tt_version_t){
        .major = major,
        .minor = minor,
        .patch = patch,
    };

    // rc=0 is stable; rc>0 maps to semver pre-release "rc.N"
    if (rc > 0)
        snprintf(version->pre, sizeof(version->pre), "rc.%" PRIu64, rc);

    return TT_OK;
}

/*============================================================================*
 * DEVICE                                                                     *
 *============================================================================*/

/// Create device from a path.
///
/// Parses device number from path and initializes device handle. The path must
/// be a device node under `/dev/tenstorrent/` (e.g., `/dev/tenstorrent/0` or
/// `/dev/tenstorrent/by-id/<board-id>`).
int tt_dev_from_path(const char *path, tt_device_t *dev) {
    // Validate args
    if (!path || !dev)
        return tt_errno = TT_EINVAL, TT_ERR;

    // Resolve symlinks to get canonical path
    char resolved[PATH_MAX];
    if (!realpath(path, resolved))
        return tt_errno = TT_ENODEV, TT_ERR;

    // Expected format: /dev/tenstorrent/<number>
    const char *prefix = "/dev/tenstorrent/";
    size_t prefix_len  = strlen(prefix);
    if (strncmp(resolved, prefix, prefix_len) != 0)
        return tt_errno = TT_EINVAL, TT_ERR;

    // Parse number
    const char *num_str = resolved + prefix_len;
    char *endptr;
    unsigned long num = strtoul(num_str, &endptr, 10);

    // Reject any trailing chars (e.g., by-id entries that don't resolve to
    // a plain numeric node).
    if (*endptr != '\0' || num > UINT32_MAX)
        return tt_errno = TT_EINVAL, TT_ERR;

    // Initialize handle
    *dev = (tt_device_t){
        .id = (uint32_t)num,
        .fd = -1,
    };

    return TT_OK;
}

/// Create device from a PCIe BDF address.
///
/// Scans connected devices and initializes the handle for the device
/// matching the given bus/device/function (BDF). Accepts DDDD:BB:DD.F or
/// BB:DD.F format.
int tt_dev_from_bdf(const char *addr, tt_device_t *dev) {
    // Validate args
    if (!addr || !dev)
        return tt_errno = TT_EINVAL, TT_ERR;

    // Parse BDF: [DDDD:]BB:DD.F
    unsigned int domain = 0, bus, slot, func;
    int n = sscanf(addr, "%x:%x:%x.%x", &domain, &bus, &slot, &func);
    if (n != 4) {
        domain = 0;
        n      = sscanf(addr, "%x:%x.%x", &bus, &slot, &func);
        if (n != 3)
            return tt_errno = TT_EINVAL, TT_ERR;
    }

    // Normalize to DDDD:BB:DD.F for comparison
    char normalized[20];
    snprintf(
        normalized,
        sizeof(normalized),
        "%04x:%02x:%02x.%x",
        domain,
        bus,
        slot,
        func
    );

    // Scan connected devices for a BDF match.
    //
    // Device numbers are not stable across resets, so BDF is the only
    // reliable identifier for a specific physical device.
    //
    // The buffer bound is an implementation convenience, not part of the
    // function's contract. It is chosen to exceed the device count of any
    // current system. A device beyond the bound would be missed, reporting
    // `TT_ENODEV`.
    tt_device_t devs[64];
    ssize_t count = tt_dev_scan(sizeof(devs) / sizeof(devs[0]), devs);
    for (ssize_t i = 0; i < count; i++) {
        if (tt_dev_open(&devs[i]) < 0)
            continue;
        tt_dev_info_t info;
        int res = tt_dev_info(&devs[i], &info);
        tt_dev_close(&devs[i]);
        if (res < 0)
            continue;

        char candidate[20];
        snprintf(
            candidate,
            sizeof(candidate),
            "%04x:%02x:%02x.%x",
            info.pci_domain,
            (info.bus_dev_fn >> 8) & 0xFF,
            (info.bus_dev_fn >> 3) & 0x1F,
            info.bus_dev_fn & 0x7
        );
        if (strcmp(candidate, normalized) == 0) {
            *dev = (tt_device_t){ .id = devs[i].id, .fd = -1 };
            return TT_OK;
        }
    }

    return tt_errno = TT_ENODEV, TT_ERR;
}

/// Scan for connected devices.
///
/// Scans `/dev/tenstorrent/` directory for character devices.
ssize_t tt_dev_scan(size_t cap, tt_device_t buf[static cap]) {
    // Scan device directory
    DIR *dir = opendir("/dev/tenstorrent");
    if (!dir)
        return tt_errno = TT_ENODEV, TT_ERR;

    // Enumerate entries
    size_t count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // Skip non-numeric entries (e.g., by-id/ symlinks, subdirs)
        if (entry->d_type != DT_CHR)
            continue;

        // Parse `id`
        char *endptr;
        uint32_t num = (uint32_t)strtoul(entry->d_name, &endptr, 10);
        if (*endptr != '\0')
            continue;

        // Record entry
        if (count < cap)
            buf[count] = (tt_device_t){ .id = num, .fd = -1 };
        count++;
    }

    // Close directory
    closedir(dir);
    return (ssize_t)count;
}

/// Open device file descriptor.
///
/// Opens `fd` if not already open. NOP if `fd >= 0`.
///
/// Uses `O_APPEND` to signal power-aware client to kernel driver.
int tt_dev_open(tt_device_t *dev) {
    // Validate args
    if (!dev)
        return tt_errno = TT_EINVAL, TT_ERR;

    // Already open
    if (dev->fd >= 0)
        return TT_OK;

    // Build path
    char path[PATH_MAX];
    size_t len = snprintf(path, sizeof(path), "/dev/tenstorrent/%u", dev->id);
    if (len < 0 || len >= sizeof(path))
        return tt_errno = TT_ENOBUFS, TT_ERR; // BUG: internal error

    // Open device.
    //
    // `O_APPEND` signals to the kernel driver that this is a power-aware
    // client; the driver initializes power to all-off for this `fd` and
    // aggregates state across all open power-aware clients.
    dev->fd = open(path, O_RDWR | O_CLOEXEC | O_APPEND);
    if (dev->fd < 0)
        return tt_errno = TT_ENODEV, TT_ERR;

    return TT_OK;
}

/// Close device file descriptor.
///
/// Closes `fd` if open. NOP if `fd < 0`. Sets `fd` to `-1` on success.
int tt_dev_close(tt_device_t *dev) {
    // Validate args
    if (!dev)
        return tt_errno = TT_EINVAL, TT_ERR;

    // Already closed
    if (dev->fd < 0)
        return TT_OK;

    // Close `fd`
    if (close(dev->fd) != 0)
        return tt_errno = TT_EIO, TT_ERR;

    // Sentinel `fd`.
    //
    // Guards against double-close and use-after-close without requiring
    // callers to track open state separately.
    dev->fd = -1;

    return TT_OK;
}

/// Get information about a device.
///
/// Queries device info via `ioctl`. Copies output struct directly.
int tt_dev_info(const tt_device_t *dev, tt_dev_info_t *info) {
    // Validate args
    if (!dev || !info)
        return tt_errno = TT_EINVAL, TT_ERR;

    // Ensure device is open
    if (dev->fd < 0)
        return tt_errno = TT_ENOTOPEN, TT_ERR;

    // Query device info
    struct tenstorrent_get_device_info query = {
        .in.output_size_bytes = sizeof(query.out),
    };
    if (ioctl(dev->fd, TENSTORRENT_IOCTL_GET_DEVICE_INFO, &query) != 0)
        return tt_errno = TT_EIO, TT_ERR;

    // Unpack output
    *info = (tt_dev_info_t){
        .output_size_bytes     = query.out.output_size_bytes,
        .vendor_id             = query.out.vendor_id,
        .device_id             = query.out.device_id,
        .subsystem_vendor_id   = query.out.subsystem_vendor_id,
        .subsystem_id          = query.out.subsystem_id,
        .bus_dev_fn            = query.out.bus_dev_fn,
        .max_dma_buf_size_log2 = query.out.max_dma_buf_size_log2,
        .pci_domain            = query.out.pci_domain,
    };

    return TT_OK;
}

/*============================================================================*
 * ADDRESSING                                                                 *
 *============================================================================*/

/// Allocate a TLB window.
///
/// Allocates TLB via `ioctl`. Does not `mmap` yet; `ptr == NULL` until
/// `tt_tlb_configure()` is called.
int tt_tlb_alloc(
    const tt_device_t *dev,
    tt_tlb_size_t size,
    tt_tlb_cache_mode_t mode,
    tt_tlb_t *tlb
) {
    // Validate args
    if (!dev || !tlb)
        return tt_errno = TT_EINVAL, TT_ERR;

    // Ensure device is open
    if (dev->fd < 0)
        return tt_errno = TT_ENOTOPEN, TT_ERR;

    // Allocate TLB
    struct tenstorrent_allocate_tlb alloc = {
        .in.size = (size_t)size,
    };
    if (ioctl(dev->fd, TENSTORRENT_IOCTL_ALLOCATE_TLB, &alloc) != 0)
        return tt_errno = TT_ENOMEM, TT_ERR;

    // Populate TLB
    *tlb = (tt_tlb_t){
        .id  = alloc.out.id,
        .ptr = NULL, // `NULL` until configured
        .len = (size_t)size,
    };

    // Determine `mmap` offset.
    //
    // The kernel returns separate offsets for uncached (UC) and write-combining
    // (WC) mappings; which one to use is fixed at alloc time.
    uint64_t offset;
    switch (mode) {
        case TT_TLB_UC:
            tlb->idx = alloc.out.mmap_offset_uc;
            break;
        case TT_TLB_WC:
            tlb->idx = alloc.out.mmap_offset_wc;
            break;
        default:
            // Invalid cache mode
            tt_errno = TT_EINVAL;
            goto cleanup;
    }

    return TT_OK;

cleanup:
    // Free the newly allocated TLB
    tt_tlb_free(dev, tlb);

failure:
    return TT_ERR;
}

/// Configure TLB mapping.
///
/// `mmap`s TLB on first call. On reconfigure, `munmap`s old and remaps new to
/// invalidate stale interior pointers.
int tt_tlb_configure(
    const tt_device_t *dev, tt_tlb_t *tlb, const tt_tlb_config_t *cfg
) {
    // Validate args
    if (!dev || !tlb || !cfg)
        return tt_errno = TT_EINVAL, TT_ERR;

    // Ensure device is open
    if (dev->fd < 0)
        return tt_errno = TT_ENOTOPEN, TT_ERR;

    // Map TLB into user space.
    //
    // On reconfigure, we `mmap` the new address before unmapping the old one.
    // This ensures kernel gives us a different virtual address, invalidating
    // any stale interior pointers users may have saved. (Traps with `SIGSEGV`
    // rather than silently accessing different device memory).
    void *ptr = mmap(
        NULL, tlb->len, PROT_READ | PROT_WRITE, MAP_SHARED, dev->fd, tlb->idx
    );
    if (ptr == MAP_FAILED) {
        // `mmap` failed
        tt_errno = TT_ENOMEM;
        // Clean up the previous mapping
        goto cleanup;
    }

    // Configure TLB via `ioctl`
    struct tenstorrent_configure_tlb mapping = {
        .in.id               = tlb->id,
        .in.config.addr      = cfg->addr,
        .in.config.x_end     = cfg->x_end,
        .in.config.y_end     = cfg->y_end,
        .in.config.x_start   = cfg->x_start,
        .in.config.y_start   = cfg->y_start,
        .in.config.noc       = cfg->noc,
        .in.config.mcast     = cfg->mcast,
        .in.config.ordering  = 1, // strict
        .in.config.linked    = cfg->linked,
        .in.config.static_vc = cfg->static_vc,
    };
    if (ioctl(dev->fd, TENSTORRENT_IOCTL_CONFIGURE_TLB, &mapping) != 0) {
        // `ioctl` failed
        tt_errno = TT_EINVAL;
        // Unmap the new mapping we just created before cleaning up the old
        // mapping (if reconfigure).
        munmap(ptr, tlb->len);
        goto cleanup;
    }

    // Unmap old mapping if this is a reconfigure
    if (tlb->ptr != NULL)
        munmap(tlb->ptr, tlb->len);

    // Update TLB with new mapping
    tlb->ptr = ptr;

    return TT_OK;

cleanup:
    // Unmap previous mapping, leaving the TLB completely unconfigured, to
    // ensure users can't accidentally use the (now) invalid mapping.
    if (tlb->ptr != NULL) {
        munmap(tlb->ptr, tlb->len);
        tlb->ptr = NULL;
    }

failure:
    return TT_ERR;
}

/// Free a TLB window.
///
/// `munmap`s TLB from user space (if mapped) then frees via `ioctl`.
int tt_tlb_free(const tt_device_t *dev, tt_tlb_t *tlb) {
    // Validate args
    if (!dev || !tlb)
        return tt_errno = TT_EINVAL, TT_ERR;

    // Ensure device is open
    if (dev->fd < 0)
        return tt_errno = TT_ENOTOPEN, TT_ERR;

    // Unmap TLB.
    //
    // Skip if unconfigured (nothing to unmap).
    if (tlb->ptr != NULL && munmap(tlb->ptr, tlb->len) != 0)
        return tt_errno = TT_EINVAL, TT_ERR;
    tlb->ptr = NULL;

    // Free TLB
    struct tenstorrent_free_tlb free = {
        .in.id = tlb->id,
    };
    if (ioctl(dev->fd, TENSTORRENT_IOCTL_FREE_TLB, &free) != 0)
        return tt_errno = TT_EINVAL, TT_ERR;
    tlb->id = 0;

    return TT_OK;
}

/*============================================================================*
 * MESSAGING                                                                  *
 *============================================================================*/

/// Send ARC message to device.
///
/// Dispatches to architecture-specific implementation based on device
/// architecture.
int tt_message(
    const tt_device_t *dev, tt_message_t *msg, bool wait, uint32_t timeout
) {
    // Validate args
    if (!dev || !msg)
        return tt_errno = TT_EINVAL, TT_ERR;

    // Ensure device is open
    if (dev->fd < 0)
        return tt_errno = TT_ENOTOPEN, TT_ERR;

    abort(); // unimplemented
}

/*============================================================================*
 * TELEMETRY                                                                  *
 *============================================================================*/

/// Get device telemetry.
///
/// Dispatches to architecture-specific implementation based on device
/// architecture.
int tt_telemetry(const tt_device_t *dev, tt_telemetry_t table) {
    // Validate args
    if (!dev || !table)
        return tt_errno = TT_EINVAL, TT_ERR;

    // Ensure device is open
    if (dev->fd < 0)
        return tt_errno = TT_ENOTOPEN, TT_ERR;

    abort(); // unimplemented
}

/*============================================================================*
 * POWER                                                                      *
 *============================================================================*/

/// Set device power state.
///
/// Issues the `SET_POWER_STATE` `ioctl` with all four defined flag bits
/// marked valid. Any flag not set in `flags` is explicitly turned off.
int tt_power(const tt_device_t *dev, uint16_t flags) {
    // Validate args
    if (!dev)
        return tt_errno = TT_EINVAL, TT_ERR;

    // Ensure open
    if (dev->fd < 0)
        return tt_errno = TT_ENOTOPEN, TT_ERR;

    // Build `ioctl` struct.
    //
    // Mark all four defined flag bits valid so the driver treats unset bits
    // as explicit off requests, not "don't care".
    struct tenstorrent_power_state power = {
        .argsz       = sizeof(power),
        .flags       = 0,
        .reserved0   = 0,
        .validity    = TT_POWER_VALIDITY(4, 0),
        .power_flags = flags,
    };

    // Issue `ioctl`
    if (ioctl(dev->fd, TENSTORRENT_IOCTL_SET_POWER_STATE, &power) != 0)
        return tt_errno = TT_EIO, TT_ERR;

    return TT_OK;
}

/*============================================================================*
 * RESET                                                                      *
 *============================================================================*/

/// Reset device.
///
/// Issues an ASIC reset, monitors sysfs for completion, then locates the
/// device by BDF (the device number may change after reset) and issues the
/// post-reset `ioctl`. Always uses a fresh `fd` so that reset works even if the
/// existing `fd` is corrupted.
int tt_reset(tt_device_t *dev) {
    // Validate args
    if (!dev)
        return tt_errno = TT_EINVAL, TT_ERR;

    // Close existing `fd`.
    //
    // Reset invalidates all `fd`s and TLBs. Ignore errors; we want reset
    // to proceed regardless of the current `fd` state.
    tt_dev_close(dev);

    // Fresh `fd`
    if (tt_dev_open(dev) < 0)
        return TT_ERR;

    // Record BDF.
    //
    // The device number may change after reset, so we record the BDF now
    // to relocate the device once it reappears.
    tt_dev_info_t info;
    if (tt_dev_info(dev, &info) < 0) {
        tt_dev_close(dev);
        return TT_ERR;
    }

    // Format BDF string
    char bdf[20];
    snprintf(
        bdf,
        sizeof(bdf),
        "%04x:%02x:%02x.%x",
        info.pci_domain,
        (info.bus_dev_fn >> 8) & 0xFF,
        (info.bus_dev_fn >> 3) & 0x1F,
        info.bus_dev_fn & 0x7
    );

    // Sysfs path for completion polling
    char sysfs_path[64];
    snprintf(sysfs_path, sizeof(sysfs_path), "/sys/bus/pci/devices/%s", bdf);

    // Issue reset
    struct tenstorrent_reset_device req = {
        .in.output_size_bytes = sizeof(req.out),
        .in.flags             = TENSTORRENT_RESET_DEVICE_ASIC_RESET,
    };
    int res = ioctl(dev->fd, TENSTORRENT_IOCTL_RESET_DEVICE, &req);
    tt_dev_close(dev);
    if (res != 0 || req.out.result != 0)
        return tt_errno = TT_EIO, TT_ERR;

    // Wait for reset completion.
    //
    // Most resets cause the device to disappear from the PCI bus and
    // reappear; we detect completion by watching for that cycle via sysfs.
    // For in-place resets (device stays on bus), we check bit 6 of PCI
    // command register (offset 4): the driver clears it when reset is done.
    bool device_disappeared = false;
    bool reset_complete     = false;
    time_t start            = time(NULL);
    while (time(NULL) - start < 5) {
        // Check sysfs presence
        if (access(sysfs_path, F_OK) == 0) {
            if (device_disappeared) {
                // Reappeared after disappearing
                reset_complete = true;
                break;
            }

            // Check in-place reset marker
            char config_path[80];
            snprintf(config_path, sizeof(config_path), "%s/config", sysfs_path);
            int cfd = open(config_path, O_RDONLY);
            if (cfd >= 0) {
                uint8_t cmd;
                if (pread(cfd, &cmd, 1, 4) == 1 && ((cmd >> 6) & 1) == 0)
                    reset_complete = true;
                close(cfd);
            }
            if (reset_complete)
                break;
        } else {
            device_disappeared = true;
        }
        usleep(100000);
    }
    if (!reset_complete)
        return tt_errno = TT_ETIMEDOUT, TT_ERR;

    // Relocate device.
    //
    // Scan by BDF; device number may have changed after reset. Allow up
    // to 10 seconds since firmware initialization can be slow.
    start          = time(NULL);
    bool relocated = false;
    while (time(NULL) - start < 10) {
        tt_device_t found;
        if (tt_dev_from_bdf(bdf, &found) == 0) {
            dev->id   = found.id;
            relocated = true;
            break;
        }
        usleep(200000);
    }
    if (!relocated)
        return tt_errno = TT_ENODEV, TT_ERR;

    // Issue post-reset
    if (tt_dev_open(dev) < 0)
        return TT_ERR;

    // Configure and issue
    req.in.flags = TENSTORRENT_RESET_DEVICE_POST_RESET;
    res          = ioctl(dev->fd, TENSTORRENT_IOCTL_RESET_DEVICE, &req);
    tt_dev_close(dev);
    if (res != 0 || req.out.result != 0)
        return tt_errno = TT_EIO, TT_ERR;

    return TT_OK;
}
