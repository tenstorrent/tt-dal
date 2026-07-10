// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

#include "ioctl.h"
#include "ttdal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

/// Reset device.
///
/// Acquires exclusive access to the device, issues an ASIC reset, monitors
/// sysfs for completion, issues the post-reset `ioctl`, and releases the
/// device. Exclusive acquisition guarantees no other client's session is
/// destroyed by the reset.
int tt_reset(const tt_device_t *dev) {
    // Validate args
    if (!dev)
        return tt_errno = TT_EINVAL, TT_ERR;

    // Acquire exclusive access.
    //
    // Requires KMD >= 2.10: the driver arbitrates access at open time as a
    // reader/writer lock, `O_EXCL` being the writer. Acquisition succeeds
    // only when no other client has the device open, so a reset never
    // destroys another client's session out from under it. `O_NONBLOCK`
    // fails with `EAGAIN` (mapped to `TT_EBUSY`) instead of waiting: a
    // blocking exclusive open can be starved by a steady stream of plain
    // opens. Older drivers silently ignore `O_EXCL`, leaving the reset
    // unfenced.
    char path[32];
    snprintf(path, sizeof(path), "/dev/tenstorrent/%u", dev->id);
    int fd = open(path, O_RDWR | O_CLOEXEC | O_APPEND | O_EXCL | O_NONBLOCK);
    if (fd < 0)
        return tt_errno = (errno == EAGAIN) ? TT_EBUSY : TT_ENODEV, TT_ERR;

    // Wrap in temporary session
    tt_session_t sess = { .dev = *dev, .fd = fd };

    // Record BDF.
    //
    // Used to locate the device's PCI config space in sysfs for completion
    // polling below.
    tt_dev_info_t info;
    if (tt_dev_info(&sess, &info) < 0) {
        tt_error_t err = tt_errno;
        close(fd);
        return tt_errno = err, TT_ERR;
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

    // Issue reset.
    //
    // Requires KMD >= 2.10: the issuing fd survives the driver's reset
    // generation bump, so the same fd issues the post-reset `ioctl` below
    // with no close/reopen window that would drop exclusivity. The reset
    // runs in place, so the device instance stays alive and the device
    // number does not change.
    struct tenstorrent_reset_device req = {
        .in.output_size_bytes = sizeof(req.out),
        .in.flags             = TENSTORRENT_RESET_DEVICE_ASIC_RESET,
    };
    if (ioctl(fd, TENSTORRENT_IOCTL_RESET_DEVICE, &req) != 0 ||
        req.out.result != 0)
        return close(fd), tt_errno = TT_EIO, TT_ERR;

    // Wait for reset completion.
    //
    // For in-place resets (device stays on bus), we check bit 6 of PCI
    // command register (offset 4): the driver clears it when reset is done.
    // If the device disappears from the bus (an out-of-band removal), wait
    // for it to reappear. The post-reset ioctl below then fails loudly on
    // the dead fd rather than targeting a re-probed instance.
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
        return close(fd), tt_errno = TT_ETIMEDOUT, TT_ERR;

    // Issue post-reset on the surviving fd
    req.in.flags = TENSTORRENT_RESET_DEVICE_POST_RESET;
    if (ioctl(fd, TENSTORRENT_IOCTL_RESET_DEVICE, &req) != 0 ||
        req.out.result != 0)
        return close(fd), tt_errno = TT_EIO, TT_ERR;

    // Release exclusive access
    if (close(fd) != 0)
        return tt_errno = TT_EIO, TT_ERR;

    return TT_OK;
}

/// Reset device via an open session.
///
/// Closes the session, then delegates to `tt_reset()`. The session is
/// consumed on all paths: a half-reset device behind a maybe-valid file
/// descriptor is a silent corruption hazard, so failing loudly and forcing
/// a fresh `tt_open()` is the safe contract.
int tt_reset_with(tt_session_t *sess) {
    // Validate args
    if (!sess)
        return tt_errno = TT_EINVAL, TT_ERR;

    // Ensure session is open
    if (sess->fd < 0)
        return tt_errno = TT_ENOTOPEN, TT_ERR;

    // Consume session.
    //
    // Exclusive acquisition in `tt_reset()` succeeds only when no other
    // descriptor is open, so the caller's own must be released first. On
    // Linux the fd is freed even when `close(2)` reports an error, so the
    // session is invalidated unconditionally to uphold the consumed-on-
    // all-paths contract.
    if (tt_close(sess) < 0)
        return sess->fd = -1, TT_ERR;

    // Reset device
    return tt_reset(&sess->dev);
}
