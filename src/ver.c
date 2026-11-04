// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "err.h"
#include "ttdal.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/// Get KMD version.
///
/// Reads the version string the kernel publishes for the loaded module, so no
/// device is opened.
int tt_kmd_version(tt_version_t *version) {
    // Validate args
    if (!version)
        return tt_fail(EINVAL);

    // Read version string.
    //
    // The kernel creates this attribute from the module's `MODULE_VERSION`, and
    // removes it when the module is unloaded.
    int fd = open("/sys/module/tenstorrent/version", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return tt_fail(errno == ENOENT ? ENODEV : errno);

    char buf[64];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    int err   = (n < 0) ? errno : EIO; // an empty read has no OS cause
    close(fd);
    if (n <= 0)
        return tt_fail(err);
    buf[n] = '\0';

    // Parse "major.minor.patch"
    tt_version_t out = {};
    int len          = 0;
    if (sscanf(
            buf,
            "%" SCNu64 ".%" SCNu64 ".%" SCNu64 "%n",
            &out.major,
            &out.minor,
            &out.patch,
            &len
        ) != 3)
        return tt_fail(EIO);

    // Parse "-pre" suffix
    const char *rest = buf + len;
    if (*rest == '-') {
        rest++;
        size_t pre = strspn(
            rest,
            "0123456789"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz"
            "-."
        );
        if (pre == 0 || pre >= sizeof(out.pre))
            return tt_fail(EIO);
        memcpy(out.pre, rest, pre);
        rest += pre;
    }
    if (strcmp(rest, "\n") != 0 && *rest != '\0')
        return tt_fail(EIO);

    // Fill output
    *version = out;

    return TT_OK;
}

/// Get firmware version.
///
/// Reads the version from a per-device attribute and parses it into a
/// `tt_version_t`.
int tt_fw_version(const tt_session_t *sess, tt_version_t *version) {
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
