// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

#include "ioctl.h"
#include "ttdal.h"

#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

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
