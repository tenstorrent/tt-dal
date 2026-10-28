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
    /// The session must have been opened with `exclusive=True`. Exclusivity
    /// can only be acquired at open time, and an exclusive session is already
    /// the sole client, so the reset never destroys another client's session
    /// out from under it. Use `Device.reset()` to reset a device this process
    /// does not hold.
    ///
    /// Raises `TTError` with:
    ///
    /// - `ENOTCONN` if the session has already been closed.
    /// - `EPERM` if the session was not opened with `exclusive=True`.
    /// - `ECONNRESET` if the device was reset or removed out-of-band.
    /// - `EBUSY` if a TLB window on the device is exported as a dma-buf.
    /// - `EIO` if the reset sequence failed.
    /// - `ETIMEDOUT` if the reset did not complete in time.
    ///
    /// Other `errno` values propagate from the failing system call.
    ///
    /// The reset sequence takes seconds, so this releases the interpreter lock
    /// while it runs.
    pub fn reset(&mut self, py: Python<'_>) -> PyResult<()> {
        self.perform(|sess| {
            // The raw pointer cannot cross threads, so pass its address.
            let sess = sess as usize;
            let (rc, errno) = py.detach(move || {
                // SAFETY: `sess` is a valid session handle, and `tt_reset_with`
                // leaves it open, so the fd stays owned by `self`.
                let rc = unsafe { ffi::tt_reset_with(sess as *const ffi::tt_session_t) };
                // Read here, before reattaching can clobber it.
                let errno = std::io::Error::last_os_error().raw_os_error().unwrap_or(0);
                (rc, errno)
            });
            if rc != 0 {
                crate::err::restore(errno);
                return Err(crate::err::fail(errno));
            }
            Ok(())
        })
    }
}

#[pymethods]
impl Device {
    /// Resets the device.
    ///
    /// Acquires exclusive access, issues the full reset sequence, and
    /// releases it. No session is required. The reset is refused while any
    /// client (including this process) has the device open.
    ///
    /// Raises `TTError` with:
    ///
    /// - `ENODEV` if the device does not exist.
    /// - `EAGAIN` if any client (including this process) has the device open.
    /// - `ECONNRESET` if the device was reset or removed out-of-band.
    /// - `EBUSY` if a TLB window on the device is exported as a dma-buf.
    /// - `EIO` if the reset sequence failed.
    /// - `ETIMEDOUT` if the reset did not complete in time.
    ///
    /// Other `errno` values propagate from the failing system call.
    ///
    /// The reset sequence takes seconds, so this releases the interpreter lock
    /// while it runs.
    #[pyo3(name = "reset")]
    pub fn reset_(&self, py: Python<'_>) -> PyResult<()> {
        let dev = self.0;
        let (rc, errno) = py.detach(move || {
            // SAFETY: `dev` is a valid device descriptor.
            let rc = unsafe { ffi::tt_reset(&raw const dev) };
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
