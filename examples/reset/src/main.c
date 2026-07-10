// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

// Example: Reset one or more Tenstorrent devices

#include <libgen.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <ttdal.h>
#include <unistd.h>

#define MAX_DEVS 64

// Resolve a device spec to a tt_device_t.
//
// Accepts a /dev/tenstorrent/N path or a PCIe bus/device/function (BDF)
// string (DDDD:BB:DD.F or
// BB:DD.F). Returns 0 with `dev->id` set, or -1 on error.
static int device_from_spec(const char *spec, tt_device_t *dev) {
    if (strchr(spec, '/'))
        return tt_dev_from_path(spec, dev);
    return tt_dev_from_bdf(spec, dev);
}

// Close a session, reporting any failure
static void close_or_warn(const char *prog, tt_session_t *sess) {
    if (tt_close(sess) < 0)
        fprintf(
            stderr,
            "%s: error: device %u: close failed: %s\n",
            prog,
            sess->dev.id,
            tt_error_describe(tt_errno)
        );
}

int main(int argc, char *argv[]) {
    char *prog = basename(argv[0]);

    int opt;
    while ((opt = getopt(argc, argv, "h")) != -1) {
        switch (opt) {
            case 'h':
                printf(
                    "Reset Tenstorrent devices.\n"
                    "\n"
                    "Usage: %s [OPTIONS] <DEVICE>...\n"
                    "\n"
                    "Arguments:\n"
                    "  <DEVICE>...  Tenstorrent device(s) to reset\n"
                    "\n"
                    "Options:\n"
                    "  -h  Show this help\n",
                    prog
                );
                return 0;
            default:
                fprintf(stderr, "Try '%s -h' for more information.\n", prog);
                return 1;
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "%s: missing operand\n", prog);
        fprintf(stderr, "Try '%s -h' for more information.\n", prog);
        return 1;
    }

    // Resolve each device spec
    tt_device_t devs[MAX_DEVS];
    int ndevs = 0;
    for (int i = optind; i < argc; i++) {
        if (ndevs >= MAX_DEVS) {
            fprintf(
                stderr, "%s: error: too many devices (max %d)\n", prog, MAX_DEVS
            );
            return 1;
        }
        if (device_from_spec(argv[i], &devs[ndevs]) < 0) {
            fprintf(
                stderr,
                "%s: error: %s: %s\n",
                prog,
                argv[i],
                tt_error_describe(tt_errno)
            );
            return 1;
        }
        ndevs++;
    }

    // Reset each device.
    //
    // `tt_reset()` acquires exclusive access internally, so a reset is
    // refused (`TT_EBUSY`) while any other client holds the device. It
    // issues the ASIC reset, waits for completion, and issues the
    // post-reset `ioctl` before returning.
    int failed = 0;
    for (int i = 0; i < ndevs; i++) {
        printf("%s: resetting device %u...\n", prog, devs[i].id);

        if (tt_reset(&devs[i]) < 0) {
            fprintf(
                stderr,
                "%s: error: device %u: %s\n",
                prog,
                devs[i].id,
                tt_error_describe(tt_errno)
            );
            failed = 1;
            continue;
        }

        // Verify device is accessible after reset
        tt_session_t sess;
        if (tt_open(&devs[i], &sess) < 0) {
            fprintf(
                stderr,
                "%s: error: device %u: post-reset open failed: %s\n",
                prog,
                devs[i].id,
                tt_error_describe(tt_errno)
            );
            failed = 1;
            continue;
        }
        tt_dev_info_t info;
        if (tt_dev_info(&sess, &info) < 0) {
            fprintf(
                stderr,
                "%s: error: device %u: post-reset verify failed: %s\n",
                prog,
                devs[i].id,
                tt_error_describe(tt_errno)
            );
            close_or_warn(prog, &sess);
            failed = 1;
            continue;
        }
        close_or_warn(prog, &sess);

        printf("%s: device %u is online.\n", prog, devs[i].id);
    }

    return failed;
}
