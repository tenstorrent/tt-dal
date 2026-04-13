//! Device reset.
//!
//! Resets the device back to its initial state.

use crate::ffi;
use pyo3::prelude::*;

use super::Session;

#[pymethods]
impl Session {
    /// Resets the device.
    ///
    /// Performing a reset invalidates all previously obtained device state. A
    /// new session must be opened to resume operations.
    ///
    /// Returns an error if the kernel driver rejects the reset request.
    pub fn reset(mut self_: PyRefMut<'_, Self>) -> PyResult<()> {
        // SAFETY: Self_ is an open device.
        crate::err::check(unsafe { ffi::tt_reset(&mut self_.0) })
    }
}
