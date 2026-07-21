// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

// Example: Discover and list all Tenstorrent devices

#include <errno.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ttdal.h>
#include <unistd.h>

#define DEVBUFSZ 256

// Collected entry: device handle plus queried info
typedef struct {
    tt_device_t dev;
    tt_dev_info_t info;
} entry_t;

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

// Compare by device ID (ascending), the default sort
static int cmp_by_id(const void *a, const void *b) {
    const entry_t *ea = a, *eb = b;
    if (ea->dev.id < eb->dev.id)
        return -1;
    if (ea->dev.id > eb->dev.id)
        return 1;
    return 0;
}

// Pack BDF into a single value for cheap comparison
static uint64_t bdf_key(const tt_dev_info_t *info) {
    uint32_t bus = (info->bus_dev_fn >> 8) & 0xFF;
    uint32_t dev = (info->bus_dev_fn >> 3) & 0x1F;
    uint32_t fn  = info->bus_dev_fn & 0x07;
    return ((uint64_t)info->pci_domain << 24) | ((uint64_t)bus << 16) |
           ((uint64_t)dev << 8) | (uint64_t)fn;
}

// Compare by PCIe BDF address (ascending)
static int cmp_by_bdf(const void *a, const void *b) {
    uint64_t ka = bdf_key(&((const entry_t *)a)->info);
    uint64_t kb = bdf_key(&((const entry_t *)b)->info);
    if (ka < kb)
        return -1;
    if (ka > kb)
        return 1;
    return 0;
}

// Print one entry in compact format: ID  Arch  BDF
static void print_short(const entry_t *e) {
    const char *arch = tt_arch_describe((tt_arch_t)e->info.device_id);
    if (!arch)
        arch = "unknown";

    uint8_t bus = (e->info.bus_dev_fn >> 8) & 0xFF;
    uint8_t dev = (e->info.bus_dev_fn >> 3) & 0x1F;
    uint8_t fn  = e->info.bus_dev_fn & 0x07;

    printf(
        "%-4u %-12s   %04x:%02x:%02x.%x\n",
        e->dev.id,
        arch,
        e->info.pci_domain,
        bus,
        dev,
        fn
    );
}

// Print one entry in long format, ls -l style: no header, all fields
static void print_long(const entry_t *e) {
    const char *arch = tt_arch_describe((tt_arch_t)e->info.device_id);
    if (!arch)
        arch = "unknown";

    uint8_t bus = (e->info.bus_dev_fn >> 8) & 0xFF;
    uint8_t dev = (e->info.bus_dev_fn >> 3) & 0x1F;
    uint8_t fn  = e->info.bus_dev_fn & 0x07;

    size_t dma_size      = 1UL << e->info.max_dma_buf_size_log2;
    const char *dma_unit = "B";
    if (dma_size >= 1024 * 1024 * 1024) {
        dma_size /= 1024 * 1024 * 1024;
        dma_unit = "GB";
    } else if (dma_size >= 1024 * 1024) {
        dma_size /= 1024 * 1024;
        dma_unit = "MB";
    } else if (dma_size >= 1024) {
        dma_size /= 1024;
        dma_unit = "KB";
    }

    char bdf[20];
    snprintf(
        bdf, sizeof(bdf), "%04x:%02x:%02x.%x", e->info.pci_domain, bus, dev, fn
    );
    printf(
        "%-4u  %-12s  0x%04x  0x%04x  %-17s  %4zu %s\n",
        e->dev.id,
        arch,
        e->info.vendor_id,
        e->info.device_id,
        bdf,
        dma_size,
        dma_unit
    );
}

int main(int argc, char *argv[]) {
    char *prog = basename(argv[0]);

    int long_fmt = 0;
    int show_hdr = 0;
    int sort_bdf = 0;
    int no_sort  = 0;
    int opt;
    while ((opt = getopt(argc, argv, "hHlBU")) != -1) {
        switch (opt) {
            case 'h':
                printf(
                    "Discover Tenstorrent devices.\n"
                    "\n"
                    "Usage: %s [OPTIONS]\n"
                    "\n"
                    "Options:\n"
                    "  -h  Show this help\n"
                    "  -l  Long listing with vendor, device, and DMA info\n"
                    "  -H  Print column headers\n"
                    "  -B  Sort by PCIe BDF address\n"
                    "  -U  Don't sort; use discovery order\n",
                    prog
                );
                return 0;
            case 'H':
                show_hdr = 1;
                break;
            case 'l':
                long_fmt = 1;
                break;
            case 'B':
                sort_bdf = 1;
                break;
            case 'U':
                no_sort = 1;
                break;
            default:
                fprintf(stderr, "Try '%s -h' for more information.\n", prog);
                return 1;
        }
    }

    if (optind != argc) {
        fprintf(stderr, "%s: unexpected argument '%s'\n", prog, argv[optind]);
        fprintf(stderr, "Try '%s -h' for more information.\n", prog);
        return 1;
    }

    tt_device_t devs[DEVBUFSZ];
    ssize_t count = tt_dev_scan(sizeof(devs) / sizeof(devs[0]), devs);

    if (count < 0) {
        fprintf(
            stderr,
            "%s: error: failed to scan devices: %s\n",
            prog,
            strerror(errno)
        );
        return 1;
    }

    if (count == 0) {
        printf("%s: no tenstorrent devices found.\n", prog);
        return 0;
    }

    size_t actual = (size_t)count < DEVBUFSZ ? (size_t)count : DEVBUFSZ;

    // Collect info for all devices before sorting
    entry_t entries[DEVBUFSZ];
    size_t nentries = 0;
    for (size_t i = 0; i < actual; i++) {
        entry_t *e = &entries[nentries];
        e->dev     = (tt_device_t){ .id = devs[i].id };

        tt_session_t sess;
        if (tt_open(&e->dev, &sess) < 0) {
            fprintf(
                stderr,
                "%s: error: device %u: failed to open: %s\n",
                prog,
                e->dev.id,
                strerror(errno)
            );
            continue;
        }

        e->info = (tt_dev_info_t){ .output_size_bytes = sizeof(e->info) };

        if (tt_dev_info(&sess, &e->info) < 0) {
            fprintf(
                stderr,
                "%s: error: device %u: failed to get info: %s\n",
                prog,
                e->dev.id,
                strerror(errno)
            );
            close_or_warn(prog, &sess);
            continue;
        }

        close_or_warn(prog, &sess);
        nentries++;
    }

    // Sort entries
    if (!no_sort)
        qsort(
            entries,
            nentries,
            sizeof(entries[0]),
            sort_bdf ? cmp_by_bdf : cmp_by_id
        );

    if (show_hdr) {
        if (long_fmt)
            printf(
                "%-4s  %-12s  %-6s  %-6s  %-17s  %s\n",
                "ID",
                "ARCH",
                "VENDOR",
                "DEVICE",
                "PCIe BDF",
                "MAX DMA"
            );
        else
            printf("%-4s %-12s   %s\n", "ID", "ARCH", "PCIe BDF");
    }

    for (size_t i = 0; i < nentries; i++) {
        if (long_fmt)
            print_long(&entries[i]);
        else
            print_short(&entries[i]);
    }

    if ((size_t)count > DEVBUFSZ) {
        printf(
            "\n%s: note: %zd additional device%s not shown\n",
            prog,
            (ssize_t)(count - DEVBUFSZ),
            (count - DEVBUFSZ) == 1 ? "" : "s"
        );
    }

    return 0;
}
