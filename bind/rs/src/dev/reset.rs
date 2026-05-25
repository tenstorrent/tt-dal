//! Device reset.
//!
//! Resets the device back to its initial state. The reset operation consumes
//! the [`Session`], enforcing at the type level that the caller cannot use the
//! session after issuing a reset. Returns an updated [`Device`] descriptor
//! reflecting any post-reset device-number change.
//!
//! # Usage
//!
//! Reset the device with [`Session::reset()`], which consumes the session and
//! returns a fresh [`Device`] descriptor.
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
    /// Closes this session, performs the reset, and returns the updated
    /// [`Device`] descriptor. The device number may have changed after reset;
    /// the returned descriptor reflects the new identifier.
    ///
    /// # Errors
    ///
    /// Returns an error if the kernel driver rejects the close or reset
    /// request.
    pub fn reset(self) -> Result<Device> {
        let mut this = std::mem::ManuallyDrop::new(self);
        // SAFETY: `ManuallyDrop` prevents `Drop` from running, so `tt_close`
        // is called exactly once here.
        err::check(unsafe { ffi::tt_close(&raw mut this.0) })?;
        let mut dev = this.0.dev;
        // SAFETY: `dev` is a valid device descriptor.
        err::check(unsafe { ffi::tt_reset(&raw mut dev) })?;
        Ok(Device(dev))
    }
}

impl Device {
    /// Resets the device.
    ///
    /// Updates `self.id` if the kernel reassigns the device number after
    /// reset. The caller must have closed any open sessions for this device
    /// before calling; live sessions are invalidated by reset.
    ///
    /// # Errors
    ///
    /// Returns an error if the kernel driver rejects the reset request.
    pub fn reset(&mut self) -> Result<()> {
        // SAFETY: `self.0` is a valid device descriptor.
        err::check(unsafe { ffi::tt_reset(self.as_mut_ptr()) })
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
