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
    /// Closes this session, then resets the underlying device as
    /// `Device.reset()` does. The session is closed on all paths, success
    /// or failure. The device number does not change, so the returned
    /// `Device` descriptor can be reopened directly.
    ///
    /// Raises `TTError` with:
    ///
    /// - `ENOTCONN` if the session has already been closed.
    /// - `ENODEV` if the device does not exist.
    /// - `EAGAIN` if another client opened the device before exclusive
    ///   access could be acquired.
    /// - `ECONNRESET` if the device was reset or removed out-of-band.
    /// - `EIO` if the reset sequence failed.
    /// - `ETIMEDOUT` if the reset did not complete in time.
    ///
    /// Other `errno` values propagate from the failing system call. The
    /// session is closed even on error.
    pub fn reset(&mut self) -> PyResult<Device> {
        // SAFETY: `self.0` is a valid session handle. tt_reset_with consumes
        // it (setting fd to -1) on all paths, so no double-close can occur.
        crate::err::check(unsafe { ffi::tt_reset_with(&mut self.0) })?;
        Ok(Device(self.0.dev))
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
    /// - `EIO` if the reset sequence failed.
    /// - `ETIMEDOUT` if the reset did not complete in time.
    ///
    /// Other `errno` values propagate from the failing system call.
    #[pyo3(name = "reset")]
    pub fn reset_(&self) -> PyResult<()> {
        // SAFETY: `self.0` is a valid device descriptor.
        crate::err::check(unsafe { ffi::tt_reset(&self.0) })
    }
}
