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
    ///
    /// The reset sequence takes seconds, so this releases the interpreter lock
    /// while it runs.
    pub fn reset(&mut self, py: Python<'_>) -> PyResult<Device> {
        // Reset a copy so the sequence can run without the interpreter lock,
        // then adopt the consumed handle back into the session.
        let mut sess = self.raw();
        let (rc, errno, sess) = py.detach(move || {
            // SAFETY: `sess` is a valid session handle. tt_reset_with consumes
            // it (setting fd to -1) on all paths, so no double-close can
            // occur.
            let rc = unsafe { ffi::tt_reset_with(&raw mut sess) };
            // Read here, before reattaching can clobber it.
            let errno = std::io::Error::last_os_error().raw_os_error().unwrap_or(0);
            (rc, errno, sess)
        });
        self.set_raw(sess);

        if rc != 0 {
            return Err(crate::err::fail(errno));
        }
        Ok(self.dev())
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
    ///
    /// The reset sequence takes seconds, so this releases the interpreter lock
    /// while it runs.
    #[pyo3(name = "reset")]
    pub fn reset_(&self, py: Python<'_>) -> PyResult<()> {
        let dev = self.0;
        let (rc, errno) = py.detach(move || {
            // SAFETY: `dev` is a valid device descriptor.
            let rc = unsafe { ffi::tt_reset(&raw const dev) };
            let errno = std::io::Error::last_os_error().raw_os_error().unwrap_or(0);
            (rc, errno)
        });
        if rc != 0 {
            return Err(crate::err::fail(errno));
        }
        Ok(())
    }
}
