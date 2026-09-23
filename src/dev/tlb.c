// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "err.h"
#include "ioctl.h"
#include "ttdal.h"

#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

/// Allocate a TLB window.
///
/// Allocates TLB via `ioctl`. Does not `mmap` yet; `ptr == NULL` until
/// `tt_tlb_bind()` is called.
int tt_tlb_alloc(
    const tt_session_t *sess,
    tt_tlb_size_t size,
    tt_tlb_cache_mode_t mode,
    tt_tlb_t *tlb
) {
    // Validate args
    if (!sess || !tlb)
        return tt_fail(EINVAL);

    // Ensure session is open
    if (sess->fd < 0)
        return tt_fail(ENOTCONN);

    // Allocate TLB
    struct tenstorrent_allocate_tlb alloc = {
        .in.size = (size_t)size,
    };
    if (ioctl(sess->fd, TENSTORRENT_IOCTL_ALLOCATE_TLB, &alloc) != 0)
        return tt_fail_io(errno);

    // Populate TLB
    *tlb = (tt_tlb_t){
        .id  = alloc.out.id,
        .ptr = NULL, // `NULL` until configured
        .len = (size_t)size,
    };

    // Determine `mmap` offset.
    //
    // The kernel returns separate offsets for uncached (UC) and write-combining
    // (WC) mappings. Which one to use is fixed at alloc time.
    uint64_t offset;
    switch (mode) {
        case TT_TLB_UC:
            tlb->idx = alloc.out.mmap_offset_uc;
            break;
        case TT_TLB_WC:
            tlb->idx = alloc.out.mmap_offset_wc;
            break;
        default:
            // Invalid cache mode
            errno = EINVAL;
            goto cleanup;
    }

    return TT_OK;

cleanup:
    // Free the newly allocated TLB, preserving the failure cause across
    // the cleanup.
    int err = errno;
    (void)tt_tlb_free(sess, tlb);
    return tt_fail(err);
}

/// Bind TLB to a NOC address.
///
/// `mmap`s TLB on first call. On rebind, `munmap`s old and remaps new to
/// invalidate stale interior pointers.
int tt_tlb_bind(
    const tt_session_t *sess, tt_tlb_t *tlb, const tt_tlb_config_t *cfg
) {
    // Validate args
    if (!sess || !tlb || !cfg)
        return tt_fail(EINVAL);

    // Ensure session is open
    if (sess->fd < 0)
        return tt_fail(ENOTCONN);

    // Map TLB into user space.
    //
    // On reconfigure, we `mmap` the new address before unmapping the old one.
    // This ensures kernel gives us a different virtual address, invalidating
    // any stale interior pointers users may have saved. (Traps with `SIGSEGV`
    // rather than silently accessing different device memory).
    void *ptr = mmap(
        NULL, tlb->len, PROT_READ | PROT_WRITE, MAP_SHARED, sess->fd, tlb->idx
    );
    if (ptr == MAP_FAILED) {
        // `mmap` failed
        tt_fail_io(errno);
        // Clean up the previous mapping
        goto cleanup;
    }

    // Configure TLB via `ioctl`
    struct tenstorrent_configure_tlb mapping = {
        .in.id               = tlb->id,
        .in.config.addr      = cfg->addr,
        .in.config.x_end     = cfg->x_end,
        .in.config.y_end     = cfg->y_end,
        .in.config.x_start   = cfg->x_start,
        .in.config.y_start   = cfg->y_start,
        .in.config.noc       = cfg->noc,
        .in.config.mcast     = cfg->mcast,
        .in.config.ordering  = 1, // strict
        .in.config.linked    = cfg->linked,
        .in.config.static_vc = cfg->static_vc,
    };
    if (ioctl(sess->fd, TENSTORRENT_IOCTL_CONFIGURE_TLB, &mapping) != 0) {
        // `ioctl` failed
        int err = errno;
        // Unmap the new mapping we just created before cleaning up the old
        // mapping (if reconfigure).
        munmap(ptr, tlb->len);
        tt_fail_io(err);
        goto cleanup;
    }

    // Unmap old mapping if this is a reconfigure
    if (tlb->ptr != NULL)
        munmap(tlb->ptr, tlb->len);

    // Update TLB with new mapping
    tlb->ptr = ptr;

    return TT_OK;

cleanup:
    // Unmap previous mapping, leaving the TLB completely unconfigured, to
    // ensure users can't accidentally use the (now) invalid mapping. The
    // failure cause is preserved across the cleanup.
    int err = errno;
    if (tlb->ptr != NULL) {
        munmap(tlb->ptr, tlb->len);
        tlb->ptr = NULL;
    }
    return tt_fail(err);
}

/// Free a TLB window.
///
/// `munmap`s TLB from user space (if mapped) then frees via `ioctl`.
int tt_tlb_free(const tt_session_t *sess, tt_tlb_t *tlb) {
    // Validate args
    if (!sess || !tlb)
        return tt_fail(EINVAL);

    // Ensure session is open
    if (sess->fd < 0)
        return tt_fail(ENOTCONN);

    // Unmap TLB.
    //
    // Skip if unconfigured (nothing to unmap).
    if (tlb->ptr != NULL && munmap(tlb->ptr, tlb->len) != 0)
        return TT_ERR;
    tlb->ptr = NULL;

    // Free TLB
    struct tenstorrent_free_tlb free = {
        .in.id = tlb->id,
    };
    if (ioctl(sess->fd, TENSTORRENT_IOCTL_FREE_TLB, &free) != 0)
        return tt_fail_io(errno);
    tlb->id = 0;

    return TT_OK;
}
