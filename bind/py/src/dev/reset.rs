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
    /// Returns an error if the kernel driver rejects the reset request, or
    /// if another client opened the device before exclusive access could be
    /// acquired. The session is closed even on error.
    pub fn reset(&mut self) -> PyResult<Device> {
        self.close()?;
        let dev = self.0.dev;
        // SAFETY: Dev is a valid device descriptor.
        crate::err::check(unsafe { ffi::tt_reset(&dev) })?;
        Ok(Device(dev))
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
    /// Returns an error if the kernel driver rejects the reset request or
    /// the device is busy.
    #[pyo3(name = "reset")]
    pub fn reset_(&self) -> PyResult<()> {
        // SAFETY: `self.0` is a valid device descriptor.
        crate::err::check(unsafe { ffi::tt_reset(&self.0) })
    }
}
