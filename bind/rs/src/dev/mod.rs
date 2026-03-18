//! Tenstorrent devices.

use crate::ffi;
pub mod arc;
pub mod power;
pub mod reset;
pub mod telem;
pub mod tlb;

use std::ffi::CString;
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
/// A lightweight, copyable descriptor that identifies a device. Holds no open
/// resources. Call [`open()`] to obtain a [`Session`] that owns a handle to the
/// device for performing operations.
///
/// [`open()`]: Self::open
#[derive(Clone, Copy, Debug)]
pub struct Device(ffi::tt_device_t);

#[expect(dead_code)]
impl Device {
    /// Returns a reference to the underlying device handle.
    pub(crate) fn as_raw(&self) -> &ffi::tt_device_t {
        &self.0
    }

    /// Returns a mutable reference to the underlying device handle.
    pub(crate) unsafe fn as_raw_mut(&mut self) -> &mut ffi::tt_device_t {
        &mut self.0
    }

    /// Returns a const raw pointer to the underlying device handle.
    pub(crate) fn as_ptr(&self) -> *const ffi::tt_device_t {
        self.as_raw()
    }

    /// Returns a mutable raw pointer to the underlying device handle.
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

    /// Constructs a device identifier from the given `/dev/tenstorrent/` path.
    fn try_from(path: &Path) -> Result<Self> {
        let path = CString::new(path.as_os_str().as_bytes())
            .map_err(|_| Error::from_raw_error(ffi::TT_EINVAL))?;
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

    /// Constructs a device identifier for the device at the given PCI address.
    fn try_from(addr: pci::Address) -> Result<Self> {
        let bdf =
            CString::new(addr.to_string()).map_err(|_| Error::from_raw_error(ffi::TT_EINVAL))?;
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
    /// # Errors
    ///
    /// Returns an error if the kernel driver scan fails.
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

        // Wrap each raw handle in the `Device` newtype
        Ok(buf.into_iter().map(Device))
    }

    /// Opens the device and returns a [`Session`] that owns the handle.
    ///
    /// # Errors
    ///
    /// Returns an error if the kernel driver fails to open the device.
    pub fn open(self) -> Result<Session> {
        let mut raw = self.0;
        // SAFETY: `raw` is a valid device handle; `tt_dev_open` fills in
        // the file descriptor.
        err::check(unsafe { ffi::tt_dev_open(&raw mut raw) })?;
        Ok(Session(raw))
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

/// Open device session.
///
/// An owned session for providing access to a device. Holds an open handle for
/// the duration of the session. Call [`Device::open()`] to obtain a `Session`
/// that is closed automatically when dropped.
#[derive(Debug)]
pub struct Session(ffi::tt_device_t);

#[expect(dead_code)]
impl Session {
    /// Returns a reference to the underlying device handle.
    pub(crate) fn as_raw(&self) -> &ffi::tt_device_t {
        &self.0
    }

    /// Returns a mutable reference to the underlying device handle.
    pub(crate) unsafe fn as_raw_mut(&mut self) -> &mut ffi::tt_device_t {
        &mut self.0
    }

    /// Returns a const raw pointer to the underlying device handle.
    pub(crate) fn as_ptr(&self) -> *const ffi::tt_device_t {
        self.as_raw()
    }

    /// Returns a mutable raw pointer to the underlying device handle.
    pub(crate) fn as_mut_ptr(&mut self) -> *mut ffi::tt_device_t {
        &raw mut self.0
    }
}

/// Session inspection.
///
/// Read-only accessors for session properties that do not require device
/// operations.
impl Session {
    /// Returns the underlying device identifier.
    #[must_use]
    pub fn dev(&self) -> Device {
        let mut raw = self.0;
        raw.fd = -1;
        Device(raw)
    }
}

impl Session {
    /// Returns static information about the device.
    ///
    /// # Errors
    ///
    /// Returns an error if the kernel driver fails to retrieve device info.
    #[expect(clippy::missing_panics_doc)]
    pub fn info(&self) -> Result<Info> {
        // SAFETY: `Info` is a C struct, so zero-initializing it is valid.
        let mut info: Info = unsafe { std::mem::zeroed() };
        info.output_size_bytes =
            u32::try_from(std::mem::size_of::<Info>()).expect("info size fits in u32");
        // SAFETY: `self.0` is an open device and `info` is a valid out-pointer.
        err::check(unsafe { ffi::tt_dev_info(self.as_ptr(), &raw mut info) })?;
        Ok(info)
    }
}

impl Drop for Session {
    fn drop(&mut self) {
        // SAFETY: `self.0` is an open device; closing it here is safe because
        // `Drop` runs exactly once when the `Session` goes out of scope.
        let _ = unsafe { ffi::tt_dev_close(&raw mut self.0) };
    }
}

#[cfg(test)]
mod tests {
    use serial_test::serial;

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
