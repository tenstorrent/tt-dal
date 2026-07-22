//! Tenstorrent devices.

use crate::ffi;
pub mod power;
pub mod reset;
pub mod smc;
pub mod telem;
pub mod tlb;

use std::cell::Cell;
use std::ffi::CString;
use std::io;
use std::mem::MaybeUninit;
use std::os::unix::ffi::OsStrExt;
use std::path::Path;

#[doc(inline)]
pub use ffi::tt_dev_info_t as Info;

use crate::{Error, Result, err};

/// Device identifier.
///
/// Unique across all connected devices.
pub type Id = u32;

/// Device descriptor.
///
/// A lightweight, copyable struct describing a device. Holds no open resources.
/// Call [`Session::open()`] to obtain a session handle for performing
/// operations on the device.
#[derive(Clone, Copy, Debug)]
pub struct Device(ffi::tt_device_t);

#[expect(dead_code)]
impl Device {
    /// Returns a reference to the underlying device descriptor.
    pub(crate) fn as_raw(&self) -> &ffi::tt_device_t {
        &self.0
    }

    /// Returns a mutable reference to the underlying device descriptor.
    pub(crate) unsafe fn as_raw_mut(&mut self) -> &mut ffi::tt_device_t {
        &mut self.0
    }

    /// Returns a const raw pointer to the underlying device descriptor.
    pub(crate) fn as_ptr(&self) -> *const ffi::tt_device_t {
        self.as_raw()
    }

    /// Returns a mutable raw pointer to the underlying device descriptor.
    pub(crate) fn as_mut_ptr(&mut self) -> *mut ffi::tt_device_t {
        &raw mut self.0
    }
}

impl PartialEq for Device {
    fn eq(&self, other: &Self) -> bool {
        self.0.id == other.0.id
    }
}

impl Eq for Device {}

impl TryFrom<&Path> for Device {
    type Error = Error;

    /// Constructs a device descriptor from the given `/dev/tenstorrent/` path.
    ///
    /// # Errors
    ///
    /// Returns [`InvalidInput`] if `path` contains an interior nul byte or
    /// does not name a `/dev/tenstorrent/` device node. Returns `ENODEV`,
    /// observable via [`Error::raw_os_error()`], if the path does not
    /// resolve to a device.
    ///
    /// [`InvalidInput`]: std::io::ErrorKind::InvalidInput
    /// [`Error::raw_os_error()`]: crate::Error::raw_os_error
    fn try_from(path: &Path) -> Result<Self> {
        let path = CString::new(path.as_os_str().as_bytes())
            .map_err(|_| Error(io::ErrorKind::InvalidInput.into()))?;
        let mut raw = MaybeUninit::<ffi::tt_device_t>::uninit();

        // SAFETY: `path` is a valid null-terminated C string and `raw` is a
        // valid out-pointer for `tt_device_t`.
        err::check(unsafe { ffi::tt_dev_from_path(path.as_ptr(), raw.as_mut_ptr()) })?;

        // SAFETY: `raw` was fully initialized by the successful call above.
        Ok(Device(unsafe { raw.assume_init() }))
    }
}

impl TryFrom<pci::Address> for Device {
    type Error = Error;

    /// Constructs a device descriptor for the device at the given PCI address.
    ///
    /// # Errors
    ///
    /// Returns `ENODEV`, observable via [`Error::raw_os_error()`], if no
    /// connected device matches the address.
    ///
    /// [`Error::raw_os_error()`]: crate::Error::raw_os_error
    fn try_from(addr: pci::Address) -> Result<Self> {
        let bdf = CString::new(addr.to_string())
            .map_err(|_| Error(io::ErrorKind::InvalidInput.into()))?;
        let mut raw = MaybeUninit::<ffi::tt_device_t>::uninit();

        // SAFETY: `bdf` is a valid null-terminated C string and `raw` is a
        // valid out-pointer for `tt_device_t`.
        err::check(unsafe { ffi::tt_dev_from_bdf(bdf.as_ptr(), raw.as_mut_ptr()) })?;

        // SAFETY: `raw` was fully initialized by the successful call above.
        Ok(Device(unsafe { raw.assume_init() }))
    }
}

/// Device lifecycle.
impl Device {
    /// Discovers all connected Tenstorrent devices.
    ///
    /// A missing device directory means the driver is not loaded, so it
    /// scans as zero devices rather than failing.
    ///
    /// # Errors
    ///
    /// Returns any `errno` left by the failing directory scan, observable
    /// via [`Error::raw_os_error()`].
    ///
    /// [`Error::raw_os_error()`]: crate::Error::raw_os_error
    #[doc(alias = "discover")]
    pub fn scan() -> Result<impl Iterator<Item = Self>> {
        // Grow-and-retry until all devices fit.
        //
        // `tt_dev_scan` returns the total device count even when it exceeds
        // `cap`. If that happens, set the new cap to at least double the
        // previous cap (taking the observed count as a floor) and retry. The
        // doubling bounds the number of iterations to O(log n) even under
        // concurrent device arrival.
        let mut cap = 32usize;
        let mut buf = Vec::with_capacity(cap);

        // Iterate until all devices scanned
        let len = loop {
            // Ensure buffer allocation is large enough
            buf.reserve(cap);

            // SAFETY: `buf` has capacity for at least `cap` elements and
            // `buf.as_mut_ptr()` is valid for writes of `cap` `tt_device_t`s.
            let found = unsafe { ffi::tt_dev_scan(cap, buf.as_mut_ptr()) };

            // Check if capacity was large enough for scan
            match usize::try_from(found) {
                // Scan failed, report error.
                Err(_) => return Err(Error::last_error()),
                // All devices fit in buffer.
                Ok(len) if len <= cap => break len,
                // Too small, grow and retry.
                Ok(len) => cap = len * 2,
            }
        };

        // SAFETY: `len` holds the exact number of devices initialized by
        // `tt_dev_scan` and does not exceed the allocated capacity.
        unsafe { buf.set_len(len) };

        // Wrap each raw descriptor in the `Device` newtype
        Ok(buf.into_iter().map(Device))
    }
}

/// Device inspection.
///
/// Read-only accessors for device properties that do not require an open
/// [`Session`].
impl Device {
    /// Returns the device identifier.
    #[must_use]
    pub fn id(&self) -> Id {
        self.0.id
    }
}

/// Open session handle.
///
/// An owned handle for performing operations on a device. Holds an open file
/// descriptor for the duration of the session. Call [`open()`] to obtain a
/// `Session` that is closed automatically when dropped.
///
/// [`open()`]: Self::open
#[derive(Debug)]
pub struct Session {
    /// Underlying session handle.
    ///
    /// Held in a `Cell` so a persistent session can reopen in place behind
    /// the shared references taken by device operations.
    raw: Cell<ffi::tt_session_t>,
    /// Options the session was opened with, reused on reopen.
    opts: OpenOptions,
}

impl Drop for Session {
    fn drop(&mut self) {
        // SAFETY: `ptr::read` copies `self`. `close` wraps it in
        // `ManuallyDrop`, preventing a second drop. The original is not used
        // after this point.
        let _ = unsafe { std::ptr::read(self) }.close();
    }
}

#[expect(dead_code)]
impl Session {
    /// Returns a copy of the underlying session handle.
    pub(crate) fn as_raw(&self) -> ffi::tt_session_t {
        self.raw.get()
    }

    /// Returns a const raw pointer to the underlying session handle.
    pub(crate) fn as_ptr(&self) -> *const ffi::tt_session_t {
        self.raw.as_ptr()
    }

    /// Returns a mutable raw pointer to the underlying session handle.
    pub(crate) fn as_mut_ptr(&self) -> *mut ffi::tt_session_t {
        self.raw.as_ptr()
    }
}

/// Session lifecycle.
impl Session {
    /// Attempts to open a session for the given device.
    ///
    /// Blocks while another client holds the device exclusively (e.g.
    /// during a reset or a flash sequence). See [`OpenOptions`] for
    /// configuring how the device is opened.
    ///
    /// # Errors
    ///
    /// Returns `ENODEV`, observable via [`Error::raw_os_error()`], if the
    /// device does not exist.
    ///
    /// [`Error::raw_os_error()`]: crate::Error::raw_os_error
    pub fn open(dev: Device) -> Result<Self> {
        Self::options().open(dev)
    }

    /// Returns a new [`OpenOptions`] object.
    ///
    /// Use this to open a session with specific options when [`open()`] is
    /// not appropriate.
    ///
    /// [`open()`]: Self::open
    #[must_use]
    pub fn options() -> OpenOptions {
        OpenOptions::new()
    }

    /// Runs a device operation, reopening a persistent session when the
    /// device connection was lost.
    pub(crate) fn call<T>(&self, mut op: impl FnMut(&Self) -> Result<T>) -> Result<T> {
        /// Reopen attempts before a persistent session gives up.
        const RETRIES: u32 = 3;

        let mut res = op(self);
        if self.opts.persist {
            for _ in 0..RETRIES {
                if !res.as_ref().is_err_and(Error::is_lost) {
                    return res;
                }
                self.reopen()?;
                res = op(self);
            }
            if res.as_ref().is_err_and(Error::is_lost) {
                // The connection keeps resetting, so report the device as
                // unusable rather than inviting another retry.
                return Err(Error(io::Error::from_raw_os_error(libc::ENODEV)));
            }
        }
        res
    }

    /// Reopens the session in place with its original options.
    fn reopen(&self) -> Result<()> {
        let dev = self.dev();
        // Release the stale descriptor.
        //
        // The close may report an error for a descriptor invalidated by an
        // out-of-band reset, but the kernel frees it regardless.
        unsafe { ffi::tt_close(self.as_mut_ptr()) };
        // SAFETY: `dev` is a valid device descriptor and the handle behind
        // `as_mut_ptr` is a valid out-pointer for `tt_session_t`.
        err::check(unsafe { ffi::tt_open(dev.as_ptr(), self.as_mut_ptr(), self.opts.flags()) })
    }
}

impl Session {
    /// Closes the session, returning any error from the driver.
    ///
    /// Prefer this over dropping when you need to observe close errors.
    ///
    /// # Errors
    ///
    /// Returns any `errno` left by the failing `close(2)`, observable via
    /// [`Error::raw_os_error()`].
    ///
    /// [`Error::raw_os_error()`]: crate::Error::raw_os_error
    pub fn close(self) -> Result<()> {
        let this = std::mem::ManuallyDrop::new(self);
        // SAFETY: `ManuallyDrop` prevents `Drop` from running, so
        // `tt_close` is called exactly once here.
        err::check(unsafe { ffi::tt_close(this.as_mut_ptr()) })
    }
}

/// Session inspection.
///
/// Read-only accessors for session properties that do not require device
/// operations.
impl Session {
    /// Returns the underlying device descriptor.
    #[must_use]
    pub fn dev(&self) -> Device {
        Device(self.raw.get().dev)
    }
}

impl Session {
    /// Returns static information about the device.
    ///
    /// # Errors
    ///
    /// Returns [`ConnectionReset`] if the session was severed by an
    /// out-of-band device reset or removal. Any other `errno` from the
    /// failing `ioctl` propagates unchanged, observable via
    /// [`Error::raw_os_error()`].
    ///
    /// [`ConnectionReset`]: std::io::ErrorKind::ConnectionReset
    /// [`Error::raw_os_error()`]: crate::Error::raw_os_error
    #[expect(clippy::missing_panics_doc)]
    pub fn info(&self) -> Result<Info> {
        self.call(|sess| {
            // SAFETY: `Info` is a C struct, so zero-initializing it is valid.
            let mut info: Info = unsafe { std::mem::zeroed() };
            info.output_size_bytes =
                u32::try_from(std::mem::size_of::<Info>()).expect("info size fits in u32");
            // SAFETY: `sess` is an open session and `info` is a valid out-pointer.
            err::check(unsafe { ffi::tt_dev_info(sess.as_ptr(), &raw mut info) })?;
            Ok(info)
        })
    }
}

/// Options which can be used to configure how a session is opened.
///
/// The [`Session::options()`] method is an alias for `OpenOptions::new()`.
/// Options are chained onto the builder, then the session is opened with
/// [`open()`]. Like [`std::fs::OpenOptions`], the builder is `Copy` and
/// reusable: a single configured value may open any number of sessions.
///
/// [`open()`]: Self::open
#[derive(Clone, Copy, Debug, Default)]
pub struct OpenOptions {
    excl: bool,
    nonblock: bool,
    persist: bool,
}

impl OpenOptions {
    /// Creates a blank new set of options ready for configuration.
    ///
    /// All options are initially set to `false`.
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    /// Sets the option for exclusive access.
    ///
    /// This option, when true, waits until no other client has the device
    /// open, then blocks all other opens for the session's lifetime.
    pub fn exclusive(&mut self, excl: bool) -> &mut Self {
        self.excl = excl;
        self
    }

    /// Sets the option for non-blocking opens.
    ///
    /// This option, when true, fails the open with [`WouldBlock`] instead
    /// of waiting for another client to release the device.
    ///
    /// [`WouldBlock`]: std::io::ErrorKind::WouldBlock
    pub fn nonblocking(&mut self, nonblock: bool) -> &mut Self {
        self.nonblock = nonblock;
        self
    }

    /// Sets the option for persistent sessions.
    ///
    /// This option, when true, transparently reopens the session with its
    /// original options and retries the failing operation when the device
    /// connection is lost to an out-of-band reset or removal. A connection
    /// that keeps resetting is retried a bounded number of times, then
    /// reported unusable with `ENODEV`. A
    /// failed reopen surfaces from the operation that triggered it.
    ///
    /// The reopen restores only the session handle: TLB allocations do not
    /// survive it, and requested power state is dropped with the stale
    /// descriptor. A device that is truly gone surfaces as the reopen's
    /// `ENODEV`, never as [`ConnectionReset`].
    ///
    /// [`ConnectionReset`]: std::io::ErrorKind::ConnectionReset
    pub fn persistent(&mut self, persist: bool) -> &mut Self {
        self.persist = persist;
        self
    }

    /// Opens a session for the device with the options specified by `self`.
    ///
    /// # Errors
    ///
    /// Returns [`WouldBlock`] if [`nonblocking`] is set and another client
    /// holds the device incompatibly. Returns `ENODEV`, observable via
    /// [`Error::raw_os_error()`], if the device does not exist.
    ///
    /// [`WouldBlock`]: std::io::ErrorKind::WouldBlock
    /// [`nonblocking`]: Self::nonblocking
    /// [`Error::raw_os_error()`]: crate::Error::raw_os_error
    pub fn open(&self, dev: Device) -> Result<Session> {
        let mut raw = MaybeUninit::<ffi::tt_session_t>::uninit();
        // SAFETY: `dev.0` is a valid device descriptor and `raw` is a valid
        // out-pointer for `tt_session_t`.
        err::check(unsafe { ffi::tt_open(&raw const dev.0, raw.as_mut_ptr(), self.flags()) })?;
        // SAFETY: `raw` was fully initialized by the successful call above.
        Ok(Session {
            raw: Cell::new(unsafe { raw.assume_init() }),
            opts: *self,
        })
    }

    /// Returns the flag bits selected by these options.
    #[expect(clippy::cast_possible_truncation)]
    fn flags(self) -> u16 {
        let mut flags = 0;
        if self.excl {
            flags |= ffi::TT_OPEN_EXCL;
        }
        if self.nonblock {
            flags |= ffi::TT_OPEN_NONBLOCK;
        }
        flags as u16
    }
}

#[cfg(test)]
mod tests {
    use crate::ffi;
    use serial_test::serial;

    #[test]
    fn options_flags() {
        let mut opts = super::OpenOptions::new();
        assert_eq!(opts.flags(), 0);
        // Persistence is binding-level, so only the other options reach C
        opts.exclusive(true).nonblocking(true).persistent(true);
        assert_eq!(
            u32::from(opts.flags()),
            ffi::TT_OPEN_EXCL | ffi::TT_OPEN_NONBLOCK
        );
    }

    #[test]
    #[ignore]
    #[serial]
    fn scan_nonempty() {
        let devs: Vec<_> = super::Device::scan().expect("device scan failed").collect();
        assert!(!devs.is_empty(), "expected at least one device");
    }

    #[test]
    #[ignore]
    #[serial]
    fn open_smoke() {
        crate::tests::open();
    }

    #[test]
    #[ignore]
    #[serial]
    fn session_dev_roundtrip() {
        let dev = crate::tests::open().dev();
        // Device IDs are contiguous small integers
        assert!(dev.id() < u32::MAX);
    }

    #[test]
    #[ignore]
    #[serial]
    fn info_smoke() {
        let info = crate::tests::open().info().expect("info failed");
        // Tenstorrent PCI vendor ID is 0x1e52
        assert_eq!(info.vendor_id, 0x1e52);
    }
}
