//! Device reset.
//!
//! Resets the device back to its initial state. Reset acquires exclusive
//! access to the device internally, so it is refused while any other client
//! holds the device. A reset never destroys another client's session out
//! from under it.
//!
//! # Usage
//!
//! Reset an unopened device with [`Device::reset()`], or consume an open
//! session with [`Session::reset()`], which enforces at the type level that
//! the caller cannot use the session after issuing a reset.
//!
//! ```no_run
//! # use ttdal::dev::Device;
//! #
//! # let dev = Device::scan().unwrap().next().unwrap();
//! #
//! // Open a device session
//! let sess = dev.open()?;
//!
//! // Reset the device, consuming the session and returning a descriptor
//! let dev = sess.reset()?;
//! #
//! # Ok::<(), ttdal::Error>(())
//! ```

use super::{Device, Session};
use crate::ffi;
use crate::{Result, err};

impl Session {
    /// Resets the device.
    ///
    /// Closes this session, then resets the underlying device as
    /// [`Device::reset()`] does. The reset runs in place, so the device
    /// number does not change and the returned [`Device`] descriptor can be
    /// reopened directly.
    ///
    /// # Errors
    ///
    /// Returns an error if the kernel driver rejects the reset request, or
    /// with `TT_EBUSY` if another client opened the device before exclusive
    /// access could be acquired. The session is consumed even on error.
    pub fn reset(self) -> Result<Device> {
        let mut this = std::mem::ManuallyDrop::new(self);
        // SAFETY: `ManuallyDrop` prevents `Drop` from running, and
        // `tt_reset_with` consumes the session (closing its fd) on all
        // paths, so the fd is closed exactly once.
        err::check(unsafe { ffi::tt_reset_with(&raw mut this.0) })?;
        Ok(Device(this.0.dev))
    }
}

impl Device {
    /// Resets the device.
    ///
    /// Acquires exclusive access, issues the full reset sequence, and
    /// releases it. No session is required.
    ///
    /// # Errors
    ///
    /// Returns an error if the kernel driver rejects the reset request, or
    /// with `TT_EBUSY` if any client (including this process) has the
    /// device open.
    pub fn reset(&self) -> Result<()> {
        // SAFETY: `self.0` is a valid device descriptor.
        err::check(unsafe { ffi::tt_reset(self.as_ptr()) })
    }
}

#[cfg(test)]
mod tests {
    use serial_test::serial;

    #[test]
    #[ignore]
    #[serial]
    fn reset_smoke() {
        let _ = crate::tests::open().reset().expect("reset failed");
    }
}
