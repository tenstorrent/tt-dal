// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "err.h"
#include "ioctl.h"
#include "trap.h"
#include "ttdal.h"

#include <stdint.h>

// SMC address space
#define CSM_BASE 0x10000000U   // core shared memory base address
#define CSM_SIZE 0x00080000U   // core shared memory size (512 KiB)
#define RESET_BASE 0x80030000U // SMC APB reset unit base address

// Wormhole
#define WH_TELEM_TAGS (RESET_BASE + 0x1D0U) // NOC_NODEID_X_0
#define WH_TELEM_DATA (RESET_BASE + 0x1D4U) // NOC_NODEID_Y_0
#define WH_SMC_NOC_X 0
#define WH_SMC_NOC_Y 2

// Blackhole
#define BH_TELEM_TAGS (RESET_BASE + 0x434U) // RESET_SCRATCH(13)
#define BH_TELEM_DATA (RESET_BASE + 0x430U) // RESET_SCRATCH(12)
#define BH_SMC_NOC_X 8
#define BH_SMC_NOC_Y 0

// Limits
#define TELEM_ENTRY_MAX 65536U // sanity bound on the entry count

// Tags
#define TELEM_HEARTBEAT 32U // TIMER_HEARTBEAT, advances each update

/// Snapshot firmware telemetry.
static ssize_t snapshot(
    const volatile uint32_t *,
    size_t,
    const volatile uint32_t *,
    size_t,
    size_t,
    tt_telemetry_t *
);

/// Get device telemetry.
ssize_t tt_telemetry(
    const tt_session_t *sess, size_t cap, tt_telemetry_t buf[static cap]
) {
    // Validate args
    if (!sess)
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
            noc_x      = WH_SMC_NOC_X;
            noc_y      = WH_SMC_NOC_Y;
            break;
        case TT_ARCH_BLACKHOLE:
            telem_tags = BH_TELEM_TAGS;
            telem_data = BH_TELEM_DATA;
            noc_x      = BH_SMC_NOC_X;
            noc_y      = BH_SMC_NOC_Y;
            break;
        default:
            return tt_fail(ENOTSUP);
    }

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

    // Compute words left in CSM
    size_t tags_len = (CSM_BASE + CSM_SIZE - ptrs.tags) / sizeof(uint32_t);
    size_t data_len = (CSM_BASE + CSM_SIZE - ptrs.data) / sizeof(uint32_t);

    // Guard the snapshot reads at the window's new mapping
    tt_trap_arm(tlb.ptr, TT_TLB_2MB);
    if (sigsetjmp(tt_trap_jmp, 1)) {
        errno = ECONNRESET;
        goto cleanup;
    }

    // Read telemetry snapshot
    ssize_t len = snapshot(tags, tags_len, data, data_len, cap, buf);
    if (len < 0)
        goto cleanup;
    tt_trap_disarm();
    (void)tt_tlb_free(sess, &tlb);
    return len;

cleanup:
    // Preserve the failure cause across the cleanup
    int err = errno;
    tt_trap_disarm();
    (void)tt_tlb_free(sess, &tlb);
    return tt_fail(err);
}

/// Snapshot firmware telemetry.
///
/// Walks the tag table at `tbl`, which may span at most `tbl_len` words, and
/// reads each value from the `data_len` words at `data`. Writes up to `cap`
/// entries to `buf`.
///
/// Retries up to 3 times on heartbeat mismatch. A stable `TIMER_HEARTBEAT`
/// before/after the read guarantees all values are from the same update cycle.
///
/// @return Number of entries, or `TT_ERR` if the table is malformed or all
///         three heartbeat attempts fail.
static ssize_t snapshot(
    const volatile uint32_t *tbl,
    size_t tbl_len,
    const volatile uint32_t *data,
    size_t data_len,
    size_t cap,
    tt_telemetry_t *buf
) {
    // Validate entry count
    if (tbl_len < 2)
        return tt_fail(EIO);
    uint32_t count = tbl[1];
    if (count > TELEM_ENTRY_MAX || count > tbl_len - 2)
        return tt_fail(EIO);

    // Locate heartbeat
    int32_t beat = -1;
    for (uint32_t idx = 0; idx < count && beat < 0; idx++) {
        uint32_t entry = tbl[2 + idx];
        uint16_t tag   = (uint16_t)(entry & 0xffff);
        uint16_t off   = (uint16_t)(entry >> 16);
        if (tag == TELEM_HEARTBEAT && off < data_len)
            beat = off;
    }

    // Attempt to read a coherent snapshot
    for (int try = 0; try < 3; try++) {
        // Fetch heartbeat
        uint32_t tick = 0;
        if (beat >= 0)
            tick = data[beat];

        // Read telemetry table
        uint64_t seen[(UINT16_MAX + 1) / 64] = { 0 };

        size_t len = 0;
        for (uint32_t idx = 0; idx < count; idx++) {
            // Read offset for tag
            uint32_t entry = tbl[2 + idx];
            uint16_t tag   = (uint16_t)(entry & 0xffff);
            uint16_t off   = (uint16_t)(entry >> 16);
            // Skip empty slots
            if (tag == 0)
                continue;
            // Ensure offset in bounds
            if (off >= data_len)
                return tt_fail(EIO);
            // Ensure tag isn't repeated
            uint64_t bit = UINT64_C(1) << (tag % 64);
            if (seen[tag / 64] & bit)
                return tt_fail(EIO);
            seen[tag / 64] |= bit;
            // Record entry
            if (len < cap)
                buf[len] = (tt_telemetry_t){ .tag = tag, .raw = data[off] };
            len++;
        }

        // Check coherency
        if (beat < 0 || tick == data[beat])
            return (ssize_t)len;
    }

    return tt_fail(EIO);
}
