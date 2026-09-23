// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

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
//! # use ttdal::dev::{Device, Session};
//! #
//! # let dev = Device::scan().unwrap().next().unwrap();
//! #
//! // Open a device session
//! let sess = Session::open(dev)?;
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
    /// Returns [`WouldBlock`] if another client opened the device before
    /// exclusive access could be acquired, [`ConnectionReset`] if the device
    /// was reset or removed out-of-band, or [`TimedOut`] if the reset did not
    /// complete in time. Returns `ENODEV` if the device is absent, `EIO` if
    /// the reset sequence failed, or `ENOTCONN` if the session is already
    /// closed, all observable via [`Error::raw_os_error()`]. Other `errno`
    /// values propagate unchanged. The session is consumed even on error.
    ///
    /// [`WouldBlock`]: std::io::ErrorKind::WouldBlock
    /// [`ConnectionReset`]: std::io::ErrorKind::ConnectionReset
    /// [`TimedOut`]: std::io::ErrorKind::TimedOut
    /// [`Error::raw_os_error()`]: crate::Error::raw_os_error
    pub fn reset(self) -> Result<Device> {
        let this = std::mem::ManuallyDrop::new(self);
        // SAFETY: `ManuallyDrop` prevents `Drop` from running, and
        // `tt_reset_with` consumes the session (closing its fd) on all
        // paths, so the fd is closed exactly once.
        err::check(unsafe { ffi::tt_reset_with(this.as_mut_ptr()) })?;
        Ok(this.dev())
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
    /// Returns [`WouldBlock`] if any client (including this process) has the
    /// device open, [`ConnectionReset`] if the device was reset or removed
    /// out-of-band, or [`TimedOut`] if the reset did not complete in time.
    /// Returns `ENODEV` if the device is absent or `EIO` if the reset
    /// sequence failed, both observable via [`Error::raw_os_error()`]. Other
    /// `errno` values propagate unchanged.
    ///
    /// [`WouldBlock`]: std::io::ErrorKind::WouldBlock
    /// [`ConnectionReset`]: std::io::ErrorKind::ConnectionReset
    /// [`TimedOut`]: std::io::ErrorKind::TimedOut
    /// [`Error::raw_os_error()`]: crate::Error::raw_os_error
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
