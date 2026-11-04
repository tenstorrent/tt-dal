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
//! Either form can be forced with [`ResetOptions::force()`], which resets
//! under any other client and invalidates its sessions. A client holding the
//! device [`exclusive`][excl] still keeps it, so a forced [`Device::reset()`]
//! is refused while one does.
//!
//! ```no_run
//! # use ttdal::dev::{Device, Session, reset::ResetOptions};
//! #
//! # let dev = Device::scan().unwrap().next().unwrap();
//! #
//! // Open a device session with exclusive access
//! let mut sess = Session::options().exclusive(true).open(dev)?;
//!
//! // Reset the device, leaving the session open
//! sess.reset(&ResetOptions::new())?;
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
    /// The session must have been opened with [`exclusive`][excl] set unless
    /// `opts` sets [`force`][force]. See the [module docs][self] for why.
    ///
    /// Takes `&mut self` so that no TLB window, [`call()`][call],
    /// [`post()`][post], or [`try_wait()`][try_wait] can be live on this
    /// session while the reset runs, since the reset invalidates every window
    /// mapped through it.
    ///
    /// # Errors
    ///
    /// Returns [`PermissionDenied`] if the session was not opened with
    /// [`exclusive`][excl] set and `opts` does not set [`force`][force],
    /// [`ConnectionReset`] if the device was reset or removed out-of-band, or
    /// [`TimedOut`] if the reset did not complete in time. Returns `EBUSY` if a
    /// TLB window on the device is exported as a dma-buf, `EIO` if the ASIC
    /// reset trigger failed, `ENOTRECOVERABLE` if the device did not
    /// reinitialize after the reset, or `ENOTCONN` if the session is closed,
    /// all observable via [`Error::raw_os_error()`]. Other `errno` values
    /// propagate unchanged.
    ///
    /// A failure with `EIO`, `ETIMEDOUT`, or `ENOTRECOVERABLE` does not leave
    /// the device as it was, and the driver has already invalidated every other
    /// client's descriptors. After `EIO` or `ETIMEDOUT` the device accepts only
    /// info queries and further resets until one completes. Retry the reset to
    /// recover. After `ENOTRECOVERABLE` the device's state is unknown, and only
    /// a later successful reset makes it usable again.
    ///
    /// While the device awaits a completed reset, every other driver request on
    /// any session reports [`ConnectionReset`], even on a session opened
    /// afterwards, so reopening does not recover it.
    ///
    /// [call]: Session::call
    /// [post]: Session::post
    /// [try_wait]: super::smc::Queued::try_wait
    /// [excl]: super::OpenOptions::exclusive
    /// [force]: ResetOptions::force
    /// [`PermissionDenied`]: std::io::ErrorKind::PermissionDenied
    /// [`ConnectionReset`]: std::io::ErrorKind::ConnectionReset
    /// [`TimedOut`]: std::io::ErrorKind::TimedOut
    /// [`Error::raw_os_error()`]: crate::Error::raw_os_error
    pub fn reset(&mut self, opts: &ResetOptions) -> Result<()> {
        // SAFETY: `self` is a valid session handle, and `tt_reset_with` leaves
        // it open.
        err::check(unsafe { ffi::tt_reset_with(self.as_ptr(), opts.flags()) })
    }
}

impl Device {
    /// Resets the device.
    ///
    /// Acquires exclusive access, issues the full reset sequence, and
    /// releases it. No session is required. With [`force`][force] set in
    /// `opts`, the device is opened shared instead, so the reset goes ahead
    /// while other clients hold it and invalidates their sessions.
    ///
    /// # Errors
    ///
    /// Returns [`WouldBlock`] if any client (including this process) has the
    /// device open, or, with [`force`][force] set, if any client holds it
    /// exclusively. Returns [`PermissionDenied`] if the caller may not open the
    /// device node. Returns [`ConnectionReset`] if the device was reset or
    /// removed out-of-band, or [`TimedOut`] if the reset did not complete in
    /// time. Returns `ENODEV` if the device is absent, `EBUSY` if a TLB window
    /// on the device is exported as a dma-buf, `EIO` if the ASIC reset trigger
    /// failed, or `ENOTRECOVERABLE` if the device did not reinitialize after
    /// the reset, all observable via [`Error::raw_os_error()`]. Other `errno`
    /// values propagate unchanged.
    ///
    /// A failure with `EIO`, `ETIMEDOUT`, or `ENOTRECOVERABLE` does not leave
    /// the device as it was. After `EIO` or `ETIMEDOUT` the device accepts only
    /// info queries and further resets until one completes. Retry the reset to
    /// recover. After `ENOTRECOVERABLE` the device's state is unknown, and only
    /// a later successful reset makes it usable again.
    ///
    /// While the device awaits a completed reset, every other driver request on
    /// any session reports [`ConnectionReset`], even on a session opened
    /// afterwards, so reopening does not recover it.
    ///
    /// [force]: ResetOptions::force
    /// [`WouldBlock`]: std::io::ErrorKind::WouldBlock
    /// [`PermissionDenied`]: std::io::ErrorKind::PermissionDenied
    /// [`ConnectionReset`]: std::io::ErrorKind::ConnectionReset
    /// [`TimedOut`]: std::io::ErrorKind::TimedOut
    /// [`Error::raw_os_error()`]: crate::Error::raw_os_error
    pub fn reset(&self, opts: &ResetOptions) -> Result<()> {
        // SAFETY: `self.0` is a valid device descriptor.
        err::check(unsafe { ffi::tt_reset(self.as_ptr(), opts.flags()) })
    }
}

/// Options which can be used to configure how a device is reset.
///
/// Options are chained onto the builder, then passed to [`Session::reset()`]
/// or [`Device::reset()`]. Like [`OpenOptions`], the builder is `Copy` and
/// reusable, so a single configured value may reset any number of devices.
///
/// [`OpenOptions`]: super::OpenOptions
#[derive(Clone, Copy, Debug, Default)]
pub struct ResetOptions {
    force: bool,
}

impl ResetOptions {
    /// Creates a blank new set of options ready for configuration.
    ///
    /// All options are initially set to `false`.
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    /// Sets the option for forced resets.
    ///
    /// This option, when true, resets the device even while other clients
    /// hold it, invalidating their sessions. A client holding the device
    /// [`exclusive`][excl] still keeps it.
    ///
    /// [excl]: super::OpenOptions::exclusive
    pub fn force(&mut self, force: bool) -> &mut Self {
        self.force = force;
        self
    }

    /// Returns the flag bits selected by these options.
    #[expect(clippy::cast_possible_truncation)]
    fn flags(self) -> u16 {
        let mut flags = 0;
        if self.force {
            flags |= ffi::TT_RESET_FORCE;
        }
        flags as u16
    }
}

#[cfg(test)]
mod tests {
    use serial_test::serial;

    use super::*;

    #[test]
    fn options_flags() {
        let mut opts = ResetOptions::new();
        assert_eq!(opts.flags(), 0);
        opts.force(true);
        assert_eq!(u32::from(opts.flags()), ffi::TT_RESET_FORCE);
    }

    #[test]
    #[ignore = "requires hardware"]
    #[serial]
    fn reset_smoke() {
        let dev = crate::tests::open().dev();
        let mut sess = Session::options()
            .exclusive(true)
            .open(dev)
            .expect("device open failed");
        sess.reset(&ResetOptions::new()).expect("reset failed");
        sess.info().expect("session lost across reset");
    }

    #[test]
    #[ignore = "requires hardware"]
    #[serial]
    fn reset_force() {
        let mut sess = crate::tests::open();
        let err = sess
            .reset(&ResetOptions::new())
            .expect_err("shared reset succeeded");
        assert_eq!(err.kind(), std::io::ErrorKind::PermissionDenied);
        sess.reset(ResetOptions::new().force(true))
            .expect("forced reset failed");
        sess.info().expect("session lost across reset");
    }
}
