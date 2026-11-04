// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

//! Device reset.
//!
//! Resets the device back to its initial state.

use crate::ffi;
use pyo3::prelude::*;

use super::{Device, Session};

#[pymethods]
impl Session {
    /// Resets the device.
    ///
    /// Runs the full reset sequence on this session's own descriptor. The
    /// driver carries that descriptor across the reset, so the session stays
    /// open and usable, whether the reset succeeds or fails. The reset runs in
    /// place, so the device number does not change. Windows mapped through the
    /// session do not survive the reset, and accessing one afterwards faults
    /// with `SIGBUS`.
    ///
    /// The session must have been opened with `exclusive=True` unless `force`
    /// is set. Exclusivity can only be acquired at open time, and an exclusive
    /// session is already the sole client, so the reset never destroys another
    /// client's session out from under it. Set `force` to reset under any
    /// other client instead, whose sessions the driver then invalidates. Use
    /// `Device.reset()` to reset a device this process does not hold.
    ///
    /// Raises `TTError` with:
    ///
    /// - `ENOTCONN` if the session has already been closed.
    /// - `EPERM` if the session was not opened with `exclusive=True` and
    ///   `force` is not set.
    /// - `ECONNRESET` if the device was reset or removed out-of-band.
    /// - `EBUSY` if a TLB window on the device is exported as a dma-buf.
    /// - `EIO` if the ASIC reset trigger failed.
    /// - `ETIMEDOUT` if the reset did not complete in time.
    /// - `ENOTRECOVERABLE` if the device did not reinitialize after the
    ///   reset.
    ///
    /// A failure with `EIO`, `ETIMEDOUT`, or `ENOTRECOVERABLE` does not leave
    /// the device as it was, and the driver has already invalidated every other
    /// client's descriptors. After `EIO` or `ETIMEDOUT` the device accepts only
    /// info queries and further resets until one completes. Retry the reset to
    /// recover. After `ENOTRECOVERABLE` the device's state is unknown, and only
    /// a later successful reset makes it usable again.
    ///
    /// While the device awaits a completed reset, every other driver request on
    /// any session reports `ECONNRESET`, even on a session opened afterwards,
    /// so reopening does not recover it.
    ///
    /// Other `errno` values propagate from the failing system call.
    ///
    /// The reset sequence takes seconds, so this releases the interpreter lock
    /// while it runs.
    #[pyo3(signature = (*, force = false))]
    pub fn reset(&mut self, py: Python<'_>, force: bool) -> PyResult<()> {
        let mut flags = 0;
        if force {
            flags |= ffi::TT_RESET_FORCE;
        }
        let flags = flags as u16;

        // The raw pointer cannot cross threads, so pass its address.
        let sess = self.as_ptr() as usize;
        let (rc, errno) = py.detach(move || {
            // SAFETY: `sess` is a valid session handle, and `tt_reset_with`
            // leaves it open, so the fd stays owned by `self`.
            let rc = unsafe { ffi::tt_reset_with(sess as *const ffi::tt_session_t, flags) };
            // Read here, before reattaching can clobber it.
            let errno = std::io::Error::last_os_error().raw_os_error().unwrap_or(0);
            (rc, errno)
        });
        if rc != 0 {
            return Err(crate::err::fail(errno));
        }
        Ok(())
    }
}

#[pymethods]
impl Device {
    /// Resets the device.
    ///
    /// Acquires exclusive access, issues the full reset sequence, and
    /// releases it. No session is required. The reset is refused while any
    /// client (including this process) has the device open. Set `force` to
    /// open the device shared instead, so the reset goes ahead while other
    /// clients hold it and invalidates their sessions.
    ///
    /// Raises `TTError` with:
    ///
    /// - `ENODEV` if the device does not exist.
    /// - `EACCES` if the caller may not open the device node.
    /// - `EAGAIN` if any client (including this process) has the device open,
    ///   or, with `force` set, if any client holds it exclusively.
    /// - `ECONNRESET` if the device was reset or removed out-of-band.
    /// - `EBUSY` if a TLB window on the device is exported as a dma-buf.
    /// - `EIO` if the ASIC reset trigger failed.
    /// - `ETIMEDOUT` if the reset did not complete in time.
    /// - `ENOTRECOVERABLE` if the device did not reinitialize after the
    ///   reset.
    ///
    /// A failure with `EIO`, `ETIMEDOUT`, or `ENOTRECOVERABLE` does not leave
    /// the device as it was. After `EIO` or `ETIMEDOUT` the device accepts only
    /// info queries and further resets until one completes. Retry the reset to
    /// recover. After `ENOTRECOVERABLE` the device's state is unknown, and only
    /// a later successful reset makes it usable again.
    ///
    /// While the device awaits a completed reset, every other driver request on
    /// any session reports `ECONNRESET`, even on a session opened afterwards,
    /// so reopening does not recover it.
    ///
    /// Other `errno` values propagate from the failing system call.
    ///
    /// The reset sequence takes seconds, so this releases the interpreter lock
    /// while it runs.
    #[pyo3(name = "reset", signature = (*, force = false))]
    pub fn reset_(&self, py: Python<'_>, force: bool) -> PyResult<()> {
        let mut flags = 0;
        if force {
            flags |= ffi::TT_RESET_FORCE;
        }
        let flags = flags as u16;

        let dev = self.0;
        let (rc, errno) = py.detach(move || {
            // SAFETY: `dev` is a valid device descriptor.
            let rc = unsafe { ffi::tt_reset(&raw const dev, flags) };
            // Read here, before reattaching can clobber it.
            let errno = std::io::Error::last_os_error().raw_os_error().unwrap_or(0);
            (rc, errno)
        });
        if rc != 0 {
            return Err(crate::err::fail(errno));
        }
        Ok(())
    }
}
