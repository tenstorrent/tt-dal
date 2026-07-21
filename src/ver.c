// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

#include "err.h"
#include "ioctl.h"
#include "ttdal.h"

#include <errno.h>
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
        return tt_fail(EINVAL);

    // Use first available device.
    //
    // Driver version is global to the kernel module, so any open `fd`
    // suffices to issue the query.
    tt_device_t dev;
    ssize_t found = tt_dev_scan(1, &dev);
    if (found < 0)
        return TT_ERR;
    if (found == 0)
        return tt_fail(ENODEV);
    tt_session_t sess;
    if (tt_open(&dev, &sess, 0) < 0)
        return TT_ERR;

    // Query driver info
    struct tenstorrent_get_driver_info query = {
        .in.output_size_bytes = sizeof(query.out),
    };
    int res = ioctl(sess.fd, TENSTORRENT_IOCTL_GET_DRIVER_INFO, &query);
    int err = errno; // saved before `tt_close` can clobber it
    tt_close(&sess);
    if (res != 0)
        return tt_fail_io(err);

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
int tt_version_firmware(const tt_session_t *sess, tt_version_t *version) {
    // Validate args
    if (!sess || !version)
        return tt_fail(EINVAL);

    // Ensure session is open
    if (sess->fd < 0)
        return tt_fail(ENOTCONN);

    // Build sysfs path
    char path[64];
    snprintf(
        path,
        sizeof(path),
        "/sys/class/tenstorrent/tenstorrent!%u/tt_fw_bundle_ver",
        sess->dev.id
    );

    // Read version string
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return TT_ERR;

    char buf[32];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    int err   = (n < 0) ? errno : EIO; // an empty read has no OS cause
    close(fd);
    if (n <= 0)
        return tt_fail(err);
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
        return tt_fail(EIO);

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
