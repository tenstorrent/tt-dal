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
    /// Closes this session, performs the reset, and returns an updated
    /// `Device` descriptor. The device number may have changed after reset;
    /// the returned descriptor reflects the new identifier.
    ///
    /// Returns an error if the kernel driver rejects the close or reset
    /// request.
    pub fn reset(&mut self) -> PyResult<Device> {
        self.close()?;
        let mut dev = self.0.dev;
        // SAFETY: Dev is a valid device descriptor.
        crate::err::check(unsafe { ffi::tt_reset(&mut dev) })?;
        Ok(Device(dev))
    }
}

#[pymethods]
impl Device {
    /// Resets the device.
    ///
    /// Updates the underlying device ID if the kernel reassigns it after
    /// reset. The caller must have closed any open sessions for this device
    /// beforehand; live sessions are invalidated by reset.
    ///
    /// Returns an error if the kernel driver rejects the reset request.
    #[pyo3(name = "reset")]
    pub fn reset_(&mut self) -> PyResult<()> {
        // SAFETY: `self.0` is a valid device descriptor.
        crate::err::check(unsafe { ffi::tt_reset(&mut self.0) })
    }
}
