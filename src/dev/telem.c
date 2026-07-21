// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

#include "err.h"
#include "ioctl.h"
#include "trap.h"
#include "ttdal.h"

#include <stdint.h>
#include <string.h>

// ARC address space
#define CSM_BASE 0x10000000U   // core shared memory base address
#define CSM_SIZE 0x00080000U   // core shared memory size (512 KiB)
#define RESET_BASE 0x80030000U // ARC APB reset unit base address

// Wormhole
#define WH_TELEM_TAGS (RESET_BASE + 0x1D0U) // NOC_NODEID_X_0
#define WH_TELEM_DATA (RESET_BASE + 0x1D4U) // NOC_NODEID_Y_0
#define WH_ARC_NOC_X 0
#define WH_ARC_NOC_Y 2

// Blackhole
#define BH_TELEM_TAGS (RESET_BASE + 0x434U) // RESET_SCRATCH(13)
#define BH_TELEM_DATA (RESET_BASE + 0x430U) // RESET_SCRATCH(12)
#define BH_ARC_NOC_X 8
#define BH_ARC_NOC_Y 0

// Limits
#define TELEM_ENTRY_CAP 128U

/// Snapshot firmware telemetry.
static int
snapshot(const volatile uint32_t *, const volatile uint32_t *, tt_telemetry_t);

/// Get device telemetry.
int tt_telemetry(const tt_session_t *sess, tt_telemetry_t table) {
    // Validate args
    if (!sess || !table)
        return tt_fail(EINVAL);

    // Ensure session is open
    if (sess->fd < 0)
        return tt_fail(ENOTCONN);

    // Fetch device info
    tt_dev_info_t info;
    if (tt_dev_info(sess, &info) != 0)
        return TT_ERR;

    // Select arch constants
    uint32_t telem_tags, telem_data;
    uint8_t noc_x, noc_y;
    switch ((tt_arch_t)info.device_id) {
        case TT_ARCH_WORMHOLE:
            telem_tags = WH_TELEM_TAGS;
            telem_data = WH_TELEM_DATA;
            noc_x      = WH_ARC_NOC_X;
            noc_y      = WH_ARC_NOC_Y;
            break;
        case TT_ARCH_BLACKHOLE:
            telem_tags = BH_TELEM_TAGS;
            telem_data = BH_TELEM_DATA;
            noc_x      = BH_ARC_NOC_X;
            noc_y      = BH_ARC_NOC_Y;
            break;
        default:
            return tt_fail(ENOTSUP);
    }

    // Zero-initialize table
    memset(table, 0, sizeof(tt_telemetry_t));

    // Allocate TLB
    tt_tlb_t tlb;
    if (tt_tlb_alloc(sess, TT_TLB_2MB, TT_TLB_UC, &tlb) != 0)
        return TT_ERR;

    // Bind TLB to APB
    tt_tlb_config_t cfg;
    cfg = (tt_tlb_config_t){
        .addr  = telem_tags & ~(uint64_t)(TT_TLB_2MB - 1),
        .x_end = noc_x,
        .y_end = noc_y,
        .noc   = 0,
    };
    if (tt_tlb_bind(sess, &tlb, &cfg) != 0)
        goto cleanup;

    // Guard the window reads.
    //
    // An out-of-band reset zaps the mapping, and the resulting fault
    // resumes here instead of killing the process.
    tt_trap_arm(tlb.ptr, TT_TLB_2MB);
    if (sigsetjmp(tt_trap_jmp, 1)) {
        errno = ECONNRESET;
        goto cleanup;
    }

    // Read CSM pointers
    struct {
        uint32_t tags, data;
    } ptrs = {
        *(volatile uint32_t *)((uint8_t *)tlb.ptr +
                               (telem_tags & (TT_TLB_2MB - 1))),
        *(volatile uint32_t *)((uint8_t *)tlb.ptr +
                               (telem_data & (TT_TLB_2MB - 1))),
    };

    // Validate CSM range
    if (ptrs.tags < CSM_BASE || ptrs.tags >= CSM_BASE + CSM_SIZE ||
        ptrs.data < CSM_BASE || ptrs.data >= CSM_BASE + CSM_SIZE) {
        errno = EIO;
        goto cleanup;
    }

    // Disarm across the rebind, which replaces the mapping
    tt_trap_disarm();

    // Bind TLB to CSM
    cfg = (tt_tlb_config_t){
        .addr  = CSM_BASE,
        .x_end = noc_x,
        .y_end = noc_y,
        .noc   = 0,
    };
    if (tt_tlb_bind(sess, &tlb, &cfg) != 0)
        goto cleanup;

    // Compute CSM pointers
    const volatile uint32_t *tags =
        (const volatile uint32_t *)((uint8_t *)tlb.ptr +
                                    (ptrs.tags & (TT_TLB_2MB - 1)));
    const volatile uint32_t *data =
        (const volatile uint32_t *)((uint8_t *)tlb.ptr +
                                    (ptrs.data & (TT_TLB_2MB - 1)));

    // Guard the snapshot reads at the window's new mapping
    tt_trap_arm(tlb.ptr, TT_TLB_2MB);
    if (sigsetjmp(tt_trap_jmp, 1)) {
        errno = ECONNRESET;
        goto cleanup;
    }

    // Read telemetry snapshot
    if (snapshot(tags, data, table) != 0)
        goto cleanup;
    tt_trap_disarm();
    tt_tlb_free(sess, &tlb);
    return TT_OK;

cleanup:
    // Preserve the failure cause across the cleanup
    int err = errno;
    tt_trap_disarm();
    tt_tlb_free(sess, &tlb);
    return tt_fail(err);
}

/// Snapshot firmware telemetry.
///
/// Retries up to 3 times on heartbeat mismatch. A stable `TIMER_HEARTBEAT`
/// before/after the read guarantees all values are from the same update cycle.
///
/// @return `TT_ERR` if all 3 heartbeat attempts fail.
static int snapshot(
    const volatile uint32_t *tbl,
    const volatile uint32_t *data,
    tt_telemetry_t table
) {
    // Clamp entry count
    uint32_t count = tbl[1];
    if (count > TELEM_ENTRY_CAP)
        count = TELEM_ENTRY_CAP;

    // Build offset map
    //
    // Uses `0xffff` as a sentinel value to denote an unset entry.
    uint16_t map[TT_TELEMETRY_LEN];
    memset(map, 0xff, sizeof(map));
    for (uint32_t idx = 0; idx < count; idx++) {
        // Read offset for tag
        uint32_t entry = tbl[2 + idx];
        uint16_t tag   = (uint16_t)(entry & 0xffff);
        uint16_t off   = (uint16_t)(entry >> 16);
        // Ensure tag in bounds
        if (tag == 0 || tag >= TT_TELEMETRY_LEN)
            continue;
        // Ensure tag isn't set
        if (map[tag] != 0xffff)
            return tt_fail(EIO);
        // Record tag offset
        map[tag] = off;
    }

    // Attempt to read a coherent snapshot
    for (int try = 0; try < 3; try++) {
        // Fetch heartbeat
        uint32_t tick = 0;
        if (map[TT_TAG_TIMER_HEARTBEAT] != 0xffff)
            tick = data[map[TT_TAG_TIMER_HEARTBEAT]];

        // Read telemetry table
        for (int tag = 1; tag < TT_TELEMETRY_LEN; tag++) {
            if (map[tag] != 0xffff)
                table[tag] = data[map[tag]];
        }

        // Check coherency
        if (map[TT_TAG_TIMER_HEARTBEAT] == 0xffff)
            return TT_OK;
        if (tick == data[map[TT_TAG_TIMER_HEARTBEAT]])
            return TT_OK;

        // Reset on mismatch
        memset(table, 0, sizeof(tt_telemetry_t));
    }

    return tt_fail(EIO);
}
