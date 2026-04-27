//! Tenstorrent devices.

use crate::ffi;
pub mod arc;
pub mod power;
pub mod reset;
pub mod telem;
pub mod tlb;

use std::ffi::CString;
use std::mem::MaybeUninit;

use pyo3::prelude::*;

/// Discovers all connected Tenstorrent devices.
///
/// Returns an error if the kernel driver scan fails.
#[pyfunction]
pub fn scan() -> PyResult<Vec<Device>> {
    // Grow-and-retry until all devices fit. `tt_dev_scan` returns the total
    // count even when it exceeds cap; if so, double the cap and retry.
    let mut cap = 32usize;
    let mut buf: Vec<ffi::tt_device_t> = Vec::new();

    let len = loop {
        buf.reserve(cap);
        // SAFETY: `buf` has capacity for at least cap elements.
        let found = unsafe { ffi::tt_dev_scan(cap, buf.as_mut_ptr()) };
        match usize::try_from(found) {
            Err(_) => return Err(crate::err::check(-1).unwrap_err()),
            Ok(len) if len <= cap => break len,
            Ok(len) => cap = len * 2,
        }
    };

    // SAFETY: `len` elements were initialized by tt_dev_scan.
    unsafe { buf.set_len(len) };
    Ok(buf.into_iter().map(Device).collect())
}

/// Device descriptor.
///
/// A lightweight, copyable descriptor that identifies a device. Holds no open
/// resources. Call `open()` to obtain a `Session` that owns a handle to the
/// device for performing operations.
#[pyclass]
pub struct Device(ffi::tt_device_t);

#[pymethods]
impl Device {
    /// Creates a device descriptor from a `/dev/tenstorrent/N` path.
    #[staticmethod]
    pub fn from_path(path: &str) -> PyResult<Self> {
        let path = CString::new(path).map_err(|_| crate::err::Error::new_err("invalid path"))?;
        let mut raw = MaybeUninit::<ffi::tt_device_t>::uninit();
        // SAFETY: `path` is a valid null-terminated C string and raw is a valid
        // out-pointer for tt_device_t.
        crate::err::check(unsafe { ffi::tt_dev_from_path(path.as_ptr(), raw.as_mut_ptr()) })?;
        // SAFETY: `raw` was fully initialized by the successful call above.
        Ok(Self(unsafe { raw.assume_init() }))
    }

    /// Creates a device descriptor from a PCIe bus/device/function (BDF)
    /// address (e.g., `"0000:03:00.0"`).
    #[staticmethod]
    pub fn from_bdf(addr: &str) -> PyResult<Self> {
        let addr = CString::new(addr).map_err(|_| crate::err::Error::new_err("invalid address"))?;
        let mut raw = MaybeUninit::<ffi::tt_device_t>::uninit();
        // SAFETY: `addr` is a valid null-terminated C string and raw is a valid
        // out-pointer for tt_device_t.
        crate::err::check(unsafe { ffi::tt_dev_from_bdf(addr.as_ptr(), raw.as_mut_ptr()) })?;
        // SAFETY: `raw` was fully initialized by the successful call above.
        Ok(Self(unsafe { raw.assume_init() }))
    }

    /// Returns the device identifier.
    #[getter]
    pub fn id(&self) -> u32 {
        self.0.id
    }

    /// Opens the device and returns a `Session` that owns the handle.
    ///
    /// Returns an error if the kernel driver fails to open the device.
    pub fn open(&self) -> PyResult<Session> {
        let mut raw = self.0;
        // SAFETY: `raw` is a valid device handle. tt_dev_open fills in the fd.
        crate::err::check(unsafe { ffi::tt_dev_open(&mut raw) })?;
        Ok(Session(raw))
    }

    fn __repr__(&self) -> String {
        format!("Device(id={})", self.0.id)
    }
}

/// Open device session.
///
/// An owned session for providing access to a device. Holds an open handle for
/// the duration of the session. Call `Device.open()` to obtain a `Session`
/// that is closed automatically when dropped. Usable as a context manager,
/// closing the session on block exit.
#[pyclass]
pub struct Session(ffi::tt_device_t);

impl Session {
    /// Returns whether the session has been closed.
    fn is_closed(&self) -> bool {
        self.0.fd < 0
    }

    /// Returns a const raw pointer to the underlying device handle.
    pub(crate) fn as_ptr(&self) -> *const ffi::tt_device_t {
        &self.0
    }
}

#[pymethods]
impl Session {
    fn __enter__(slf: PyRef<'_, Self>) -> PyRef<'_, Self> {
        slf
    }

    fn __exit__(
        &mut self,
        _exc_type: &Bound<'_, PyAny>,
        _exc_val: &Bound<'_, PyAny>,
        _exc_tb: &Bound<'_, PyAny>,
    ) -> bool {
        let _ = self.close();
        false
    }

    /// Closes the session explicitly.
    ///
    /// Idempotent, so closing an already-closed session is a no-op.
    pub fn close(&mut self) -> PyResult<()> {
        if !self.is_closed() {
            // SAFETY: `raw` is an open device. Closing exactly once is safe.
            crate::err::check(unsafe { ffi::tt_dev_close(&mut self.0) })?;
        }
        Ok(())
    }

    /// Returns the underlying device descriptor.
    pub fn dev(&self) -> Device {
        let mut raw = self.0;
        raw.fd = -1;
        Device(raw)
    }

    /// Returns static information about the device.
    ///
    /// Returns an error if the kernel driver fails to retrieve device info.
    pub fn info(&self) -> PyResult<Info> {
        // SAFETY: Zeroing tt_dev_info_t is valid. All fields are plain integers.
        let mut info: ffi::tt_dev_info_t = unsafe { std::mem::zeroed() };
        info.output_size_bytes = std::mem::size_of::<ffi::tt_dev_info_t>() as u32;
        // SAFETY: Self is an open device and info is a valid out-pointer.
        crate::err::check(unsafe { ffi::tt_dev_info(Session::as_ptr(self), &mut info) })?;
        Ok(Info(info))
    }

    fn __repr__(&self) -> String {
        if self.is_closed() {
            "Session(closed)".to_string()
        } else {
            format!("Session(id={})", self.0.id)
        }
    }
}

impl Drop for Session {
    fn drop(&mut self) {
        let _ = self.close();
    }
}

/// Static device information.
#[pyclass]
pub struct Info(ffi::tt_dev_info_t);

#[pymethods]
impl Info {
    #[getter]
    fn vendor_id(&self) -> u16 {
        self.0.vendor_id
    }
    #[getter]
    fn device_id(&self) -> u16 {
        self.0.device_id
    }
    #[getter]
    fn subsystem_vendor_id(&self) -> u16 {
        self.0.subsystem_vendor_id
    }
    #[getter]
    fn subsystem_id(&self) -> u16 {
        self.0.subsystem_id
    }
    #[getter]
    fn bus_dev_fn(&self) -> u16 {
        self.0.bus_dev_fn
    }
    #[getter]
    fn pci_domain(&self) -> u16 {
        self.0.pci_domain
    }
    #[getter]
    fn max_dma_buf_size_log2(&self) -> u16 {
        self.0.max_dma_buf_size_log2
    }

    fn __repr__(&self) -> String {
        format!(
            "Info(vendor={:#06x}, device={:#06x})",
            self.0.vendor_id, self.0.device_id
        )
    }
}
