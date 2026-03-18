//! Device reset.
//!
//! Resets the device back to its initial state. The reset operation consumes
//! the [`Session`], enforcing at the type level that the caller cannot use the
//! session after issuing a reset. A new session must be opened via
//! [`Device::open()`](super::Device::open) to resume operations.
//!
//! # Usage
//!
//! Reset the device with [`Session::reset()`], which consumes the session.
//!
//! ```no_run
//! # use ttdal::dev::Device;
//! #
//! # let dev = Device::scan().unwrap().next().unwrap();
//! #
//! // Open a device session
//! let con = dev.open()?;
//!
//! // Reset the device; session is consumed
//! con.reset()?;
//! #
//! # Ok::<(), ttdal::Error>(())
//! ```

use crate::ffi;
use super::Session;
use crate::{Result, err};

impl Session {
    /// Resets the device.
    ///
    /// Performing a reset invalidates all previously obtained device state. A
    /// new session must be opened via [`Device::open()`](super::Device::open)
    /// to resume operations.
    ///
    /// # Errors
    ///
    /// Returns an error if the kernel driver rejects the reset request.
    pub fn reset(mut self) -> Result<()> {
        // SAFETY: `self.0` is an open device.
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
        crate::tests::open().reset().expect("reset failed");
    }
}
