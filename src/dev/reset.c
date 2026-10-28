// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "clock.h"
#include "err.h"
#include "ioctl.h"
#include "ttdal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define RESET_TIMEOUT_MS 5000   // budget for the device to signal completion
#define POLL_INTERVAL_US 100000 // sleep between completion checks
#define POST_RESET_DELAY_MS 500 // firmware time to publish telemetry

/// Run the reset sequence on an open session.
///
/// Issues the ASIC reset, waits for completion, then issues the post-reset
/// `ioctl` on the same descriptor. Leaves the session open.
static int reset(const tt_session_t *sess) {
    // Record BDF.
    //
    // Used to locate the device's PCI config space in sysfs for completion
    // polling below.
    tt_dev_info_t info;
    if (tt_dev_info(sess, &info) < 0)
        return TT_ERR;

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
    // Requires KMD >= 2.10: the issuing descriptor is carried across the
    // driver's reset generation bump, so it survives to issue the post-reset
    // `ioctl` below and stays valid afterwards. Every other descriptor open
    // on the device is invalidated. The reset runs in place, so the device
    // instance stays alive and the device number does not change.
    struct tenstorrent_reset_device req = {
        .in.output_size_bytes = sizeof(req.out),
        .in.flags             = TENSTORRENT_RESET_DEVICE_ASIC_RESET,
    };
    if (ioctl(sess->fd, TENSTORRENT_IOCTL_RESET_DEVICE, &req) != 0)
        return tt_fail_io(errno);
    if (req.out.result != 0)
        return tt_fail(EIO); // the ASIC reset trigger failed

    // Wait for reset completion.
    //
    // For in-place resets (device stays on bus), we check bit 6 of PCI command
    // register (offset 4): the hardware clears it when reset is done. If the
    // device disappears from the bus (an out-of-band removal), wait for it to
    // reappear. The post-reset ioctl below then fails loudly on the dead fd
    // rather than targeting a re-probed instance.
    bool device_disappeared = false;
    bool reset_complete     = false;
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    while (elapsed_ms(&start) < RESET_TIMEOUT_MS) {
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
            int cfd = open(config_path, O_RDONLY | O_CLOEXEC);
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
        usleep(POLL_INTERVAL_US);
    }
    if (!reset_complete)
        return tt_fail(ETIMEDOUT);

    // Wait for firmware.
    //
    // Works around a defect in tt-kmd 2.11.0 and earlier: the post-reset
    // `ioctl` probes firmware telemetry exactly once, and firmware publishes it
    // about 350 ms after the marker clears. Issued any sooner, the `ioctl`
    // leaves the driver without telemetry until the next reset or a module
    // reload.
    //
    // FIXME: Remove once tt-kmd waits for firmware itself.
    struct timespec delay = { .tv_nsec = POST_RESET_DELAY_MS * 1000000L };
    while (nanosleep(&delay, &delay) < 0 && errno == EINTR)
        continue; // resume the rest after a handled signal

    // Issue post-reset on the surviving descriptor
    req.in.flags = TENSTORRENT_RESET_DEVICE_POST_RESET;
    if (ioctl(sess->fd, TENSTORRENT_IOCTL_RESET_DEVICE, &req) != 0)
        return tt_fail_io(errno);
    if (req.out.result != 0)
        return tt_fail(ENOTRECOVERABLE); // the device did not reinitialize

    return TT_OK;
}

/// Reset device.
///
/// Acquires exclusive access to the device, runs the reset sequence, and
/// releases the device. Exclusive acquisition guarantees no other client's
/// session is destroyed by the reset. A forced reset opens the device shared
/// instead.
int tt_reset(const tt_device_t *dev, uint16_t flags) {
    // Validate args
    if (!dev)
        return tt_fail(EINVAL);

    // Reject unknown flags
    if (flags & ~TT_RESET_FORCE)
        return tt_fail(EINVAL);

    // Acquire access.
    //
    // Exclusive acquisition succeeds only when no other client has the
    // device open, so a reset never destroys another client's session out
    // from under it. A forced reset opens shared instead, which succeeds
    // alongside other shared clients but not while another client holds the
    // device exclusively. Non-blocking either way, because a blocking
    // exclusive open can be starved by a steady stream of shared opens, and a
    // blocking shared open would wait out an exclusive holder. On older
    // drivers `TT_OPEN_EXCL` is silently ignored, leaving the reset unfenced.
    uint16_t open_flags = TT_OPEN_NONBLOCK;
    if (!(flags & TT_RESET_FORCE))
        open_flags |= TT_OPEN_EXCL;
    tt_session_t sess;
    if (tt_open(dev, &sess, open_flags) < 0)
        return TT_ERR;

    // Reset device
    int rc  = reset(&sess);
    int err = errno; // saved before `tt_close` can clobber it

    // Release the device
    if (tt_close(&sess) < 0 && rc == TT_OK)
        return TT_ERR;

    return rc < 0 ? tt_fail(err) : TT_OK;
}

/// Reset device via an open session.
///
/// Runs the reset sequence on the session's own descriptor. The driver
/// carries that descriptor across the reset, so the session stays open and
/// usable. Unless the reset is forced, the session must hold the device
/// exclusively, so the reset never destroys another client's session out from
/// under it.
int tt_reset_with(const tt_session_t *sess, uint16_t flags) {
    // Validate args
    if (!sess)
        return tt_fail(EINVAL);

    // Reject unknown flags
    if (flags & ~TT_RESET_FORCE)
        return tt_fail(EINVAL);

    // Ensure session is open
    if (sess->fd < 0)
        return tt_fail(ENOTCONN);

    // Require exclusive access unless forced.
    //
    // Exclusivity can only be acquired at open time, so it is read from the
    // flags the session was opened with. An exclusive session is the sole
    // client, so the reset cannot invalidate another client's descriptors. A
    // forced reset skips the check and resets under any other client.
    if (!(flags & TT_RESET_FORCE) && !(sess->flags & TT_OPEN_EXCL))
        return tt_fail(EPERM);

    // Reset device
    return reset(sess);
}
