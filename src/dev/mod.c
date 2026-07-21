// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

#include "err.h"
#include "ioctl.h"
#include "ttdal.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/// Create device from a path.
///
/// Parses device number from path and initializes device descriptor. The path
/// must be a device node under `/dev/tenstorrent/` (e.g., `/dev/tenstorrent/0`
/// or `/dev/tenstorrent/by-id/<board-id>`).
int tt_dev_from_path(const char *path, tt_device_t *dev) {
    // Validate args
    if (!path || !dev)
        return tt_fail(EINVAL);

    // Resolve symlinks to get canonical path
    char resolved[PATH_MAX];
    if (!realpath(path, resolved))
        return tt_fail(ENODEV);

    // Expected format: /dev/tenstorrent/<number>
    const char *prefix = "/dev/tenstorrent/";
    size_t prefix_len  = strlen(prefix);
    if (strncmp(resolved, prefix, prefix_len) != 0)
        return tt_fail(EINVAL);

    // Parse number
    const char *num_str = resolved + prefix_len;
    char *endptr;
    unsigned long num = strtoul(num_str, &endptr, 10);

    // Reject any trailing chars (e.g., by-id entries that don't resolve to
    // a plain numeric node).
    if (*endptr != '\0' || num > UINT32_MAX)
        return tt_fail(EINVAL);

    // Initialize descriptor
    *dev = (tt_device_t){ .id = (uint32_t)num };

    return TT_OK;
}

/// Create device from a PCIe BDF address.
///
/// Scans connected devices and initializes the descriptor for the device
/// matching the given bus/device/function (BDF). Accepts DDDD:BB:DD.F or
/// BB:DD.F format.
int tt_dev_from_bdf(const char *addr, tt_device_t *dev) {
    // Validate args
    if (!addr || !dev)
        return tt_fail(EINVAL);

    // Parse BDF: [DDDD:]BB:DD.F
    unsigned int domain = 0, bus, slot, func;
    int n = sscanf(addr, "%x:%x:%x.%x", &domain, &bus, &slot, &func);
    if (n != 4) {
        domain = 0;
        n      = sscanf(addr, "%x:%x.%x", &bus, &slot, &func);
        if (n != 3)
            return tt_fail(EINVAL);
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
    // `ENODEV`.
    tt_device_t devs[64];
    ssize_t count = tt_dev_scan(sizeof(devs) / sizeof(devs[0]), devs);
    for (ssize_t i = 0; i < count; i++) {
        tt_session_t sess;
        if (tt_open(&devs[i], &sess, 0) < 0)
            continue;
        tt_dev_info_t info;
        int res = tt_dev_info(&sess, &info);
        tt_close(&sess);
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
            *dev = (tt_device_t){ .id = devs[i].id };
            return TT_OK;
        }
    }

    return tt_fail(ENODEV);
}

/// Scan for connected devices.
///
/// Scans `/dev/tenstorrent/` directory for character devices.
ssize_t tt_dev_scan(size_t cap, tt_device_t buf[static cap]) {
    // Scan device directory.
    //
    // A missing directory means the driver is not loaded, so it scans as
    // zero devices rather than failing.
    DIR *dir = opendir("/dev/tenstorrent");
    if (!dir)
        return (errno == ENOENT) ? 0 : TT_ERR;

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
            buf[count] = (tt_device_t){ .id = num };
        count++;
    }

    // Close directory
    closedir(dir);
    return (ssize_t)count;
}

/// Open a session for a device.
///
/// Opens `/dev/tenstorrent/<id>` and initializes the session.
///
/// Uses `O_APPEND` to signal a power-aware client to the kernel driver.
int tt_open(const tt_device_t *dev, tt_session_t *sess, uint16_t flags) {
    // Validate args
    if (!dev || !sess)
        return tt_fail(EINVAL);

    // Reject unknown flags.
    //
    // No flags are currently defined.
    if (flags != 0)
        return tt_fail(EINVAL);

    // Build path
    char path[PATH_MAX];
    int len = snprintf(path, sizeof(path), "/dev/tenstorrent/%u", dev->id);
    if (len < 0 || (size_t)len >= sizeof(path))
        return tt_fail(EIO); // BUG: internal error

    // Open device.
    //
    // `O_APPEND` signals to the kernel driver that this is a power-aware
    // client. The driver initializes power to all-off for this `fd` and
    // aggregates state across all open power-aware clients.
    int fd = open(path, O_RDWR | O_CLOEXEC | O_APPEND);
    if (fd < 0)
        return tt_fail(ENODEV);

    // Initialize session
    *sess = (tt_session_t){ .dev = *dev, .fd = fd };

    return TT_OK;
}

/// Close a session.
///
/// Closes `fd` if open. NOP if `fd < 0`. Sets `fd` to `-1` on success.
int tt_close(tt_session_t *sess) {
    // Validate args
    if (!sess)
        return tt_fail(EINVAL);

    // Already closed
    if (sess->fd < 0)
        return TT_OK;

    // Close `fd`
    if (close(sess->fd) != 0)
        return TT_ERR;

    // Sentinel `fd`.
    //
    // Guards against double-close and use-after-close without requiring
    // callers to track open state separately.
    sess->fd = -1;

    return TT_OK;
}

/// Get information about a device.
///
/// Queries device info via `ioctl`. Copies output struct directly.
int tt_dev_info(const tt_session_t *sess, tt_dev_info_t *info) {
    // Validate args
    if (!sess || !info)
        return tt_fail(EINVAL);

    // Ensure session is open
    if (sess->fd < 0)
        return tt_fail(ENOTCONN);

    // Query device info
    struct tenstorrent_get_device_info query = {
        .in.output_size_bytes = sizeof(query.out),
    };
    if (ioctl(sess->fd, TENSTORRENT_IOCTL_GET_DEVICE_INFO, &query) != 0)
        return tt_fail_io(errno);

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
