//! Memory windows.
//!
//! Translation Lookaside Buffer (TLB) windows map a region of device NOC
//! address space into host-accessible memory. Access follows a three-step
//! lifetime chain:
//!
//! 1. Allocate a `Tlb` via `Session.alloc()`, specifying window size and
//!    caching.
//! 2. Bind the `Tlb` to a NOC address via `Tlb.bind()`, obtaining a `Window`.
//! 3. Read or write device memory through the `Window`.

use crate::ffi;
use pyo3::exceptions::{PyIndexError, PyRuntimeError, PyValueError};
use pyo3::prelude::*;

use super::Session;

/// TLB window size.
///
/// Not all sizes are available on all architectures. The kernel rejects
/// unsupported sizes at allocation time.
#[pyclass(eq, from_py_object)]
#[derive(Clone, Copy, PartialEq)]
pub enum Size {
    /// 1 MB window.
    ///
    /// Supported: Wormhole.
    Mb1,
    /// 2 MB window.
    ///
    /// Supported: Wormhole, Blackhole.
    Mb2,
    /// 16 MB window.
    ///
    /// Supported: Wormhole.
    Mb16,
    /// 4 GB window.
    ///
    /// Supported: Blackhole.
    Gb4,
}

impl Size {
    /// Returns the raw TLB size constant.
    pub(crate) fn as_raw(&self) -> u64 {
        match self {
            Self::Mb1 => ffi::TT_TLB_1MB,
            Self::Mb2 => ffi::TT_TLB_2MB,
            Self::Mb16 => ffi::TT_TLB_16MB,
            Self::Gb4 => ffi::TT_TLB_4GB,
        }
    }
}

/// TLB cache mode.
#[pyclass(eq, from_py_object)]
#[derive(Clone, Copy, PartialEq)]
pub enum Caching {
    /// Uncached.
    ///
    /// Use for register access where ordering and immediate visibility matter.
    Uncached,
    /// Write-combined.
    ///
    /// Use for memory access where batching writes improves performance.
    WriteCombined,
}

impl Caching {
    /// Returns the raw cache mode constant.
    pub(crate) fn as_raw(&self) -> u32 {
        match self {
            Self::Uncached => ffi::TT_TLB_UC,
            Self::WriteCombined => ffi::TT_TLB_WC,
        }
    }
}

/// TLB window configuration.
#[pyclass(skip_from_py_object)]
#[derive(Clone)]
pub struct Config {
    #[pyo3(get)]
    pub addr: u64,
    #[pyo3(get)]
    pub x_end: u8,
    #[pyo3(get)]
    pub y_end: u8,
    #[pyo3(get)]
    pub x_start: u8,
    #[pyo3(get)]
    pub y_start: u8,
    #[pyo3(get)]
    pub noc: u8,
    #[pyo3(get)]
    pub mcast: bool,
    #[pyo3(get)]
    pub linked: bool,
    #[pyo3(get)]
    pub static_vc: u8,
}

#[pymethods]
impl Config {
    #[new]
    #[pyo3(signature = (addr, x_end, y_end, x_start=0, y_start=0, noc=0, mcast=false, linked=false, static_vc=0))]
    #[expect(clippy::too_many_arguments)]
    fn new(
        addr: u64,
        x_end: u8,
        y_end: u8,
        x_start: u8,
        y_start: u8,
        noc: u8,
        mcast: bool,
        linked: bool,
        static_vc: u8,
    ) -> Self {
        Self {
            addr,
            x_end,
            y_end,
            x_start,
            y_start,
            noc,
            mcast,
            linked,
            static_vc,
        }
    }

    fn __repr__(&self) -> String {
        format!(
            "Config(addr={:#018x}, x_end={}, y_end={}, noc={})",
            self.addr, self.x_end, self.y_end, self.noc
        )
    }
}

/// An allocated TLB window bound to a `Session`.
///
/// The TLB is freed automatically when dropped. The window is not mapped until
/// `Tlb.bind()` is called. Usable as a context manager, freeing the TLB on
/// block exit.
#[pyclass(unsendable)]
pub struct Tlb {
    pub(crate) raw: ffi::tt_tlb_t,
    pub(crate) sess: Py<Session>,
    pub(crate) freed: bool,
}

#[pymethods]
impl Tlb {
    fn __enter__(slf: PyRef<'_, Self>) -> PyRef<'_, Self> {
        slf
    }

    fn __exit__(
        &mut self,
        py: Python<'_>,
        _exc_type: &Bound<'_, PyAny>,
        _exc_val: &Bound<'_, PyAny>,
        _exc_tb: &Bound<'_, PyAny>,
    ) -> bool {
        let _ = self.free(py);
        false
    }

    /// Frees the TLB window explicitly.
    ///
    /// Idempotent, so freeing an already-freed TLB is a no-op.
    ///
    /// Raises `TTError` with:
    ///
    /// - `ENOTCONN` if the session has been closed.
    /// - `ECONNRESET` if the session was severed by an out-of-band device
    ///   reset or removal.
    ///
    /// Other `errno` values propagate from the failing system call.
    pub fn free(&mut self, py: Python<'_>) -> PyResult<()> {
        if !self.freed {
            self.freed = true;
            let sess = self.sess.borrow(py);
            // SAFETY: `raw` was allocated by tt_tlb_alloc. tt_tlb_free handles
            // closed sessions.
            crate::err::check(unsafe { ffi::tt_tlb_free(Session::as_ptr(&sess), &mut self.raw) })?;
        }
        Ok(())
    }

    /// Maps the TLB to the given NOC address and coordinates, returning a
    /// `Window` for memory access.
    ///
    /// Safe to call again after the previous `Window` has been dropped.
    ///
    /// Raises `RuntimeError` if the TLB has already been freed. Otherwise
    /// raises `TTError` with:
    ///
    /// - `ENOTCONN` if the session has been closed.
    /// - `ECONNRESET` if the session was severed by an out-of-band device
    ///   reset or removal.
    ///
    /// Other `errno` values propagate from the failing system call.
    fn bind(mut self_: PyRefMut<'_, Self>, cfg: &Config, py: Python<'_>) -> PyResult<Window> {
        if self_.freed {
            return Err(PyRuntimeError::new_err("TLB has been freed"));
        }
        let dev_ptr = {
            let sess = self_.sess.borrow(py);
            Session::as_ptr(&sess)
        };
        let raw = ffi::tt_tlb_config_t {
            addr: cfg.addr,
            x_end: cfg.x_end,
            y_end: cfg.y_end,
            x_start: cfg.x_start,
            y_start: cfg.y_start,
            noc: cfg.noc,
            mcast: cfg.mcast,
            linked: cfg.linked,
            static_vc: cfg.static_vc,
        };
        // SAFETY: `dev_ptr` is an open device, self_.raw was allocated by
        // tt_tlb_alloc, and cfg is a valid configuration.
        crate::err::check(unsafe { ffi::tt_tlb_bind(dev_ptr, &mut self_.raw, &raw) })?;
        // SAFETY: `self_.as_ptr()` is a borrowed Python object pointer.
        // from_borrowed_ptr increments the refcount to give us an owned
        // Py<Tlb>.
        let tlb: Py<Tlb> = unsafe {
            pyo3::Bound::<pyo3::PyAny>::from_borrowed_ptr(py, self_.as_ptr())
                .cast_unchecked::<Tlb>()
                .clone()
                .unbind()
        };
        Ok(Window { tlb })
    }

    fn __repr__(&self) -> String {
        if self.freed {
            "Tlb(freed)".to_string()
        } else {
            format!("Tlb(id={})", self.raw.id)
        }
    }
}

impl Drop for Tlb {
    fn drop(&mut self) {
        let _ = Python::try_attach(|py| self.free(py));
    }
}

/// A mapped TLB window bound to a `Tlb`.
///
/// Provides volatile read/write access to device memory. Usable as a context
/// manager, though the mapping is released when the owning `Tlb` is freed,
/// not on block exit.
#[pyclass(unsendable)]
pub struct Window {
    tlb: Py<Tlb>,
}

#[pymethods]
impl Window {
    fn __enter__(slf: PyRef<'_, Self>) -> PyRef<'_, Self> {
        slf
    }

    fn __exit__(
        &mut self,
        _exc_type: &Bound<'_, PyAny>,
        _exc_val: &Bound<'_, PyAny>,
        _exc_tb: &Bound<'_, PyAny>,
    ) -> bool {
        false
    }

    /// Returns the window size in bytes.
    #[getter]
    fn size(&self, py: Python<'_>) -> PyResult<usize> {
        Ok(self.tlb.borrow(py).raw.len)
    }

    /// Reads a u8 at the given byte offset.
    ///
    /// Raises `IndexError` if `offset` is out of bounds.
    fn read_u8(&self, offset: usize, py: Python<'_>) -> PyResult<u8> {
        let tlb = self.tlb.borrow(py);
        if offset >= tlb.raw.len {
            return Err(PyIndexError::new_err("offset out of bounds"));
        }
        let ptr = tlb.raw.ptr;
        drop(tlb);
        // SAFETY: `ptr` is valid (checked above), offset is in-bounds.
        Ok(unsafe { (ptr as *const u8).add(offset).read_volatile() })
    }

    /// Reads a u32 at the given byte offset (must be 4-byte aligned).
    ///
    /// Raises `ValueError` if `offset` is not 4-byte aligned, or `IndexError`
    /// if it is out of bounds.
    fn read_u32(&self, offset: usize, py: Python<'_>) -> PyResult<u32> {
        let tlb = self.tlb.borrow(py);
        if !offset.is_multiple_of(4) {
            return Err(PyValueError::new_err("offset must be 4-byte aligned"));
        }
        if offset + 4 > tlb.raw.len {
            return Err(PyIndexError::new_err("offset out of bounds"));
        }
        let ptr = tlb.raw.ptr;
        drop(tlb);
        // SAFETY: `ptr` is valid, offset is in-bounds and aligned.
        Ok(unsafe { ((ptr as *const u8).add(offset) as *const u32).read_volatile() })
    }

    /// Reads a u64 at the given byte offset (must be 8-byte aligned).
    ///
    /// Raises `ValueError` if `offset` is not 8-byte aligned, or `IndexError`
    /// if it is out of bounds.
    fn read_u64(&self, offset: usize, py: Python<'_>) -> PyResult<u64> {
        let tlb = self.tlb.borrow(py);
        if !offset.is_multiple_of(8) {
            return Err(PyValueError::new_err("offset must be 8-byte aligned"));
        }
        if offset + 8 > tlb.raw.len {
            return Err(PyIndexError::new_err("offset out of bounds"));
        }
        let ptr = tlb.raw.ptr;
        drop(tlb);
        // SAFETY: `ptr` is valid, offset is in-bounds and aligned.
        Ok(unsafe { ((ptr as *const u8).add(offset) as *const u64).read_volatile() })
    }

    /// Writes a u8 at the given byte offset.
    ///
    /// Raises `IndexError` if `offset` is out of bounds.
    fn write_u8(&self, offset: usize, value: u8, py: Python<'_>) -> PyResult<()> {
        let tlb = self.tlb.borrow(py);
        if offset >= tlb.raw.len {
            return Err(PyIndexError::new_err("offset out of bounds"));
        }
        let ptr = tlb.raw.ptr;
        drop(tlb);
        // SAFETY: `ptr` is valid, offset is in-bounds.
        unsafe { (ptr as *mut u8).add(offset).write_volatile(value) };
        Ok(())
    }

    /// Writes a u32 at the given byte offset (must be 4-byte aligned).
    ///
    /// Raises `ValueError` if `offset` is not 4-byte aligned, or `IndexError`
    /// if it is out of bounds.
    fn write_u32(&self, offset: usize, value: u32, py: Python<'_>) -> PyResult<()> {
        let tlb = self.tlb.borrow(py);
        if !offset.is_multiple_of(4) {
            return Err(PyValueError::new_err("offset must be 4-byte aligned"));
        }
        if offset + 4 > tlb.raw.len {
            return Err(PyIndexError::new_err("offset out of bounds"));
        }
        let ptr = tlb.raw.ptr;
        drop(tlb);
        // SAFETY: `ptr` is valid, offset is in-bounds and aligned.
        unsafe { ((ptr as *mut u8).add(offset) as *mut u32).write_volatile(value) };
        Ok(())
    }

    /// Writes a u64 at the given byte offset (must be 8-byte aligned).
    ///
    /// Raises `ValueError` if `offset` is not 8-byte aligned, or `IndexError`
    /// if it is out of bounds.
    fn write_u64(&self, offset: usize, value: u64, py: Python<'_>) -> PyResult<()> {
        let tlb = self.tlb.borrow(py);
        if !offset.is_multiple_of(8) {
            return Err(PyValueError::new_err("offset must be 8-byte aligned"));
        }
        if offset + 8 > tlb.raw.len {
            return Err(PyIndexError::new_err("offset out of bounds"));
        }
        let ptr = tlb.raw.ptr;
        drop(tlb);
        // SAFETY: `ptr` is valid, offset is in-bounds and aligned.
        unsafe { ((ptr as *mut u8).add(offset) as *mut u64).write_volatile(value) };
        Ok(())
    }

    fn __repr__(&self) -> String {
        "Window".to_string()
    }
}

use std::mem::MaybeUninit;

#[pymethods]
impl Session {
    /// Allocates a TLB window of the given size and cache mode.
    ///
    /// The window is not mapped until `Tlb.bind()` is called.
    ///
    /// Raises `TTError` with:
    ///
    /// - `ENOTCONN` if the session has been closed.
    /// - `ECONNRESET` if the session was severed by an out-of-band device
    ///   reset or removal.
    ///
    /// Other `errno` values propagate from the failing system call.
    pub fn alloc(
        mut slf: PyRefMut<'_, Self>,
        size: Size,
        caching: Caching,
        py: Python<'_>,
    ) -> PyResult<Tlb> {
        let raw = slf.call(|sess| {
            let mut raw = MaybeUninit::<ffi::tt_tlb_t>::uninit();
            // SAFETY: `sess` is an open device and raw is a valid out-pointer.
            crate::err::check(unsafe {
                ffi::tt_tlb_alloc(sess, size.as_raw(), caching.as_raw(), raw.as_mut_ptr())
            })?;
            // SAFETY: `raw` was fully initialized by the successful call above.
            Ok(unsafe { raw.assume_init() })
        })?;
        // SAFETY: `slf.as_ptr()` is a borrowed Python object pointer.
        // from_borrowed_ptr increments the refcount to give us an owned
        // Py<Session>.
        let sess: Py<Session> = unsafe {
            pyo3::Bound::<pyo3::PyAny>::from_borrowed_ptr(py, slf.as_ptr())
                .cast_unchecked::<Session>()
                .clone()
                .unbind()
        };
        Ok(Tlb {
            raw,
            sess,
            freed: false,
        })
    }
}
