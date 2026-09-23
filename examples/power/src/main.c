// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Example: Interactive power state control for a Tenstorrent device

#include <errno.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <ttdal.h>
#include <unistd.h>

#define MAX_DEVS 64

// Resolve a device spec to a tt_device_t.
//
// Accepts a /dev/tenstorrent/N path or a PCI BDF string (DDDD:BB:DD.F or
// BB:DD.F). Returns 0 with `dev->id` set, or -1 on error.
static int device_from_spec(const char *spec, tt_device_t *dev) {
    if (strchr(spec, '/'))
        return tt_dev_from_path(spec, dev);
    return tt_dev_from_bdf(spec, dev);
}

// Table of controllable power flags
static const struct {
    tt_power_flag_t flag;
    const char *name;
} FLAGS[] = {
    { TT_POWER_MAX_AI_CLK,       "MAX_AI_CLK      " },
    { TT_POWER_MRISC_PHY_WAKEUP, "MRISC_PHY_WAKEUP" },
    { TT_POWER_TENSIX_ENABLE,    "TENSIX_ENABLE   " },
    { TT_POWER_L2CPU_ENABLE,     "L2CPU_ENABLE    " },
};

#define NFLAGS ((int)(sizeof(FLAGS) / sizeof(FLAGS[0])))

static void
print_state(int ndevs, tt_device_t devs[static ndevs], uint16_t mask) {
    printf("\npower control: devices");
    for (int i = 0; i < ndevs; i++)
        printf("%s %u", i == 0 ? "" : ",", devs[i].id);
    printf("\n\n");

    for (int i = 0; i < NFLAGS; i++) {
        const char *val = (mask & FLAGS[i].flag) ? "on " : "off";
        printf("  %d  %s  [ %s ]\n", i + 1, FLAGS[i].name, val);
    }

    printf("\n");
}

// Close a session, reporting any failure
static void close_or_warn(const char *prog, tt_session_t *sess) {
    if (tt_close(sess) < 0)
        fprintf(
            stderr,
            "%s: error: device %u: close failed: %s\n",
            prog,
            sess->dev.id,
            strerror(errno)
        );
}

int main(int argc, char *argv[]) {
    char *prog = basename(argv[0]);

    int opt;
    while ((opt = getopt(argc, argv, "h")) != -1) {
        switch (opt) {
            case 'h':
                printf(
                    "Control power of Tenstorrent devices.\n"
                    "\n"
                    "Usage: %s [OPTIONS] <DEVICE>...\n"
                    "\n"
                    "Arguments:\n"
                    "  <DEVICE>...  Tenstorrent device(s) to control\n"
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
                stderr, "%s: error: %s: %s\n", prog, argv[i], strerror(errno)
            );
            return 1;
        }
        ndevs++;
    }

    // Open a session for each device
    tt_session_t sessions[MAX_DEVS];
    for (int i = 0; i < ndevs; i++) {
        if (tt_open(&devs[i], &sessions[i], 0) < 0) {
            fprintf(
                stderr,
                "%s: error: device %u: %s\n",
                prog,
                devs[i].id,
                strerror(errno)
            );
            for (int j = 0; j < i; j++)
                close_or_warn(prog, &sessions[j]);
            return 1;
        }
    }

    // Interactive loop.
    //
    // Each toggle is applied to all devices immediately. The driver aggregates
    // state across all open power-aware clients and removes our contribution
    // on close.
    uint16_t mask = 0;
    char line[64];
    print_state(ndevs, devs, mask);
    while (1) {
        printf("toggle (1-%d), p to print, h for help, q to quit: ", NFLAGS);
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin))
            break;

        // Strip trailing newline to get the bare input
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n')
            line[--len] = '\0';

        // Empty input: redraw state
        if (len == 0) {
            print_state(ndevs, devs, mask);
            continue;
        }

        // Handle quit
        if (strcmp(line, "q") == 0 || strcmp(line, "Q") == 0)
            break;

        // Print current state
        if (strcmp(line, "p") == 0 || strcmp(line, "P") == 0 ||
            strcmp(line, "print") == 0) {
            print_state(ndevs, devs, mask);
            continue;
        }

        // Show help
        if (strcmp(line, "h") == 0 || strcmp(line, "H") == 0 ||
            strcmp(line, "help") == 0) {
            printf("\ncommands:\n");
            printf("  1-%d   toggle power flag by number\n", NFLAGS);
            printf("  p     print current power state\n");
            printf("  h     show this help\n");
            printf("  q     quit\n");
            printf("  <enter>  redraw current state\n\n");
            continue;
        }

        // Parse index
        int idx = line[0] - '1';
        if (idx < 0 || idx >= NFLAGS || line[1] != '\0') {
            printf(
                "unknown command '%s'. enter 1-%d, p, h, or q.\n", line, NFLAGS
            );
            continue;
        }

        // Toggle and apply to all devices.
        //
        // On any failure revert the `mask` and report. Partial failure leaves
        // devices inconsistent but that is the operator's problem to fix.
        uint16_t new_mask = mask ^ (uint16_t)FLAGS[idx].flag;
        int ok            = 1;
        for (int i = 0; i < ndevs; i++) {
            if (tt_power(&sessions[i], new_mask) < 0) {
                fprintf(
                    stderr,
                    "%s: error: device %u: %s\n",
                    prog,
                    devs[i].id,
                    strerror(errno)
                );
                ok = 0;
            }
        }
        if (ok)
            mask = new_mask;

        print_state(ndevs, devs, mask);
    }

    // Close all sessions
    for (int i = 0; i < ndevs; i++)
        close_or_warn(prog, &sessions[i]);

    return 0;
}
