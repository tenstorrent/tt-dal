// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

//! Device reset.
//!
//! Resets the device back to its initial state.
//!
//! # Usage
//!
//! Reset an unopened device with [`Device::reset()`], which acquires
//! exclusive access internally and is refused while any other client holds
//! the device. Reset a device already held with [`Session::reset()`], which
//! runs on the session's own descriptor and leaves it open.
//!
//! [`Session::reset()`] requires a session opened with [`exclusive`][excl]
//! set. Exclusivity can only be acquired at [`Session::open()`][open], and an
//! exclusive session is already the sole client, so neither form destroys
//! another client's session out from under it.
//!
//! ```no_run
//! # use ttdal::dev::{Device, Session};
//! #
//! # let dev = Device::scan().unwrap().next().unwrap();
//! #
//! // Open a device session with exclusive access
//! let mut sess = Session::options().exclusive(true).open(dev)?;
//!
//! // Reset the device, leaving the session open
//! sess.reset()?;
//! #
//! # Ok::<(), ttdal::Error>(())
//! ```
//!
//! [open]: Session::open
//! [excl]: super::OpenOptions::exclusive

use super::{Device, Session};
use crate::ffi;
use crate::{Result, err};

impl Session {
    /// Resets the device.
    ///
    /// Runs the full reset sequence on this session's own descriptor. The
    /// driver carries that descriptor across the reset, so the session stays
    /// open and usable, whether the reset succeeds or fails. The reset runs in
    /// place, so the device number does not change.
    ///
    /// The session must have been opened with [`exclusive`][excl] set. See
    /// the [module docs][self] for why.
    ///
    /// Takes `&mut self` so that no TLB window, [`call()`][call],
    /// [`post()`][post], or [`try_wait()`][try_wait] can be live on this
    /// session while the reset runs, since the reset invalidates every window
    /// mapped through it.
    ///
    /// # Errors
    ///
    /// Returns [`PermissionDenied`] if the session was not opened with
    /// [`exclusive`][excl] set, [`ConnectionReset`] if the device was reset or
    /// removed out-of-band, or [`TimedOut`] if the reset did not complete in
    /// time. Returns `EBUSY` if a TLB window on the device is exported as a
    /// dma-buf, `EIO` if the reset sequence failed, or `ENOTCONN` if the
    /// session is closed, all observable via [`Error::raw_os_error()`]. Other
    /// `errno` values propagate unchanged.
    ///
    /// [call]: Session::call
    /// [post]: Session::post
    /// [try_wait]: super::smc::Queued::try_wait
    /// [excl]: super::OpenOptions::exclusive
    /// [`PermissionDenied`]: std::io::ErrorKind::PermissionDenied
    /// [`ConnectionReset`]: std::io::ErrorKind::ConnectionReset
    /// [`TimedOut`]: std::io::ErrorKind::TimedOut
    /// [`Error::raw_os_error()`]: crate::Error::raw_os_error
    pub fn reset(&mut self) -> Result<()> {
        self.perform(|sess| {
            // SAFETY: `sess` is a valid session handle, and `tt_reset_with`
            // leaves it open.
            err::check(unsafe { ffi::tt_reset_with(sess.as_ptr()) })
        })
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
    /// Returns `ENODEV` if the device is absent, `EBUSY` if a TLB window on the
    /// device is exported as a dma-buf, or `EIO` if the reset sequence failed,
    /// all observable via [`Error::raw_os_error()`]. Other `errno` values
    /// propagate unchanged.
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

    use super::*;

    #[test]
    #[ignore = "requires hardware"]
    #[serial]
    fn reset_smoke() {
        let dev = crate::tests::open().dev();
        let mut sess = Session::options()
            .exclusive(true)
            .open(dev)
            .expect("device open failed");
        sess.reset().expect("reset failed");
        sess.info().expect("session lost across reset");
    }
}
