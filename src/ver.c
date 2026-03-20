// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

#include "ioctl.h"
#include "ttdal.h"

#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

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

    // rc=0 is stable. rc>0 maps to semver pre-release "rc.N"
    if (rc > 0)
        snprintf(version->pre, sizeof(version->pre), "rc.%" PRIu64, rc);

    return TT_OK;
}
