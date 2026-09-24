// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

//! Tenstorrent devices.

use crate::ffi;
pub mod power;
pub mod reset;
pub mod smc;
pub mod telem;
pub mod tlb;

use std::ffi::CString;
use std::mem::MaybeUninit;

use pyo3::prelude::*;

/// Discovers all connected Tenstorrent devices.
///
/// A missing device directory means the driver is not loaded, so it scans
/// as zero devices rather than failing.
///
/// Raises `TTError` carrying any `errno` left by the failing directory
/// scan.
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
/// A lightweight, copyable struct describing a device. Holds no open resources.
/// Call `open()` to obtain a `Session` handle for performing operations on
/// the device.
#[pyclass]
pub struct Device(pub(crate) ffi::tt_device_t);

#[pymethods]
impl Device {
    /// Creates a device descriptor from a `/dev/tenstorrent/N` path.
    ///
    /// Raises `TTError` with:
    ///
    /// - `EINVAL` if `path` contains an interior nul byte or does not name a
    ///   `/dev/tenstorrent/` device node.
    /// - `ENODEV` if the path does not resolve to a device.
    #[staticmethod]
    pub fn from_path(path: &str) -> PyResult<Self> {
        let path = CString::new(path)
            .map_err(|_| crate::err::TTError::new_err((libc::EINVAL, "invalid path")))?;
        let mut raw = MaybeUninit::<ffi::tt_device_t>::uninit();
        // SAFETY: `path` is a valid null-terminated C string and raw is a valid
        // out-pointer for tt_device_t.
        crate::err::check(unsafe { ffi::tt_dev_from_path(path.as_ptr(), raw.as_mut_ptr()) })?;
        // SAFETY: `raw` was fully initialized by the successful call above.
        Ok(Self(unsafe { raw.assume_init() }))
    }

    /// Creates a device descriptor from a PCIe bus/device/function (BDF)
    /// address (e.g., `"0000:03:00.0"`).
    ///
    /// Raises `TTError` with:
    ///
    /// - `EINVAL` if `addr` contains an interior nul byte or is malformed.
    /// - `ENODEV` if no connected device matches the address.
    #[staticmethod]
    pub fn from_bdf(addr: &str) -> PyResult<Self> {
        let addr = CString::new(addr)
            .map_err(|_| crate::err::TTError::new_err((libc::EINVAL, "invalid address")))?;
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
    /// Blocks while another client holds the device exclusively unless
    /// `nonblocking` is set. Set `exclusive` to wait for sole access, then
    /// block other opens for the session's lifetime.
    ///
    /// Set `persistent` to transparently reopen the session and retry the
    /// failing operation when the device connection is lost to an
    /// out-of-band reset or removal. A device that is truly gone surfaces
    /// as the reopen's `ENODEV`, never as `ECONNRESET`, and a connection
    /// that keeps resetting is retried a bounded number of times before
    /// reporting `ENODEV` as well.
    ///
    /// Raises `TTError` with:
    ///
    /// - `ENODEV` if the device could not be opened.
    /// - `EAGAIN` if `nonblocking` is set and another client holds the
    ///   device incompatibly.
    ///
    /// A blocking open waits for as long as the other client holds the device,
    /// so this releases the interpreter lock while it waits.
    #[pyo3(signature = (*, exclusive = false, nonblocking = false, persistent = false))]
    pub fn open(
        &self,
        py: Python<'_>,
        exclusive: bool,
        nonblocking: bool,
        persistent: bool,
    ) -> PyResult<Session> {
        let mut flags = 0;
        if exclusive {
            flags |= ffi::TT_OPEN_EXCL;
        }
        if nonblocking {
            flags |= ffi::TT_OPEN_NONBLOCK;
        }
        let flags = flags as u16;

        // Opened on a copy of the descriptor so the wait can run without the
        // interpreter lock.
        let dev = self.0;
        let (rc, errno, sess) = py.detach(move || {
            let mut sess = MaybeUninit::<ffi::tt_session_t>::uninit();
            // SAFETY: `dev` is a valid device descriptor and `sess` is a valid
            // out-pointer for tt_session_t.
            let rc = unsafe { ffi::tt_open(&raw const dev, sess.as_mut_ptr(), flags) };
            // Read here, before reattaching can clobber it.
            let errno = std::io::Error::last_os_error().raw_os_error().unwrap_or(0);
            (rc, errno, sess)
        });

        if rc != 0 {
            return Err(crate::err::fail(errno));
        }
        Ok(Session {
            // SAFETY: `sess` was fully initialized by the successful call
            // above.
            raw: unsafe { sess.assume_init() },
            persist: persistent,
        })
    }

    fn __repr__(&self) -> String {
        format!("Device(id={})", self.0.id)
    }
}

/// Open session handle.
///
/// An owned handle for performing operations on a device. Holds an open file
/// descriptor for the duration of the session. Call `Device.open()` to
/// obtain a `Session` that is closed automatically when dropped. Usable as a
/// context manager, closing the session on block exit.
#[pyclass]
pub struct Session {
    /// Underlying session handle.
    raw: ffi::tt_session_t,
    /// Whether the session reopens on a lost device connection.
    persist: bool,
}

impl Session {
    /// Returns whether the session has been closed.
    fn is_closed(&self) -> bool {
        self.raw.fd < 0
    }

    /// Returns a copy of the underlying session handle.
    ///
    /// The handle is plain data, so a copy can cross a `Python::detach`
    /// boundary that a raw pointer cannot.
    pub(crate) fn raw(&self) -> ffi::tt_session_t {
        self.raw
    }

    /// Replaces the underlying session handle.
    ///
    /// Used to adopt a handle that a call mutated on a copy while the
    /// interpreter lock was released.
    pub(crate) fn set_raw(&mut self, raw: ffi::tt_session_t) {
        self.raw = raw;
    }

    /// Returns a const raw pointer to the underlying session handle.
    pub(crate) fn as_ptr(&self) -> *const ffi::tt_session_t {
        &self.raw
    }

    /// Returns a mutable raw pointer to the underlying session handle.
    pub(crate) fn as_mut_ptr(&mut self) -> *mut ffi::tt_session_t {
        &raw mut self.raw
    }

    /// Runs a device operation, reopening a persistent session when the
    /// device connection was lost.
    pub(crate) fn perform<T>(
        &mut self,
        mut op: impl FnMut(*const ffi::tt_session_t) -> PyResult<T>,
    ) -> PyResult<T> {
        /// Reopen attempts before a persistent session gives up.
        const RETRIES: u32 = 3;

        let mut res = op(&raw const self.raw);
        if self.persist {
            for _ in 0..RETRIES {
                if !(res.is_err() && crate::err::lost()) {
                    return res;
                }
                self.reopen()?;
                res = op(&raw const self.raw);
            }
            if res.is_err() && crate::err::lost() {
                // The connection keeps resetting, so report the device as
                // unusable rather than inviting another retry.
                return Err(crate::err::fail(libc::ENODEV));
            }
        }
        res
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
    ///
    /// Raises `TTError` carrying any `errno` left by the failing `close(2)`.
    pub fn close(&mut self) -> PyResult<()> {
        if !self.is_closed() {
            // SAFETY: `self.raw` is an open session. Closing exactly once
            // is safe.
            crate::err::check(unsafe { ffi::tt_close(self.as_mut_ptr()) })?;
        }
        Ok(())
    }

    /// Reopens the session in place with its original flags.
    ///
    /// Closes the current descriptor and reopens the same device with the
    /// options this session was opened with. Use this to recover a session
    /// severed by an out-of-band reset: the reopen succeeds after a reset and
    /// raises `ENODEV` after a removal.
    ///
    /// The reopen restores only the session handle. TLB allocations do not
    /// survive it, and any requested power state is dropped with the old
    /// descriptor. A persistent session reopens on its own, so calling this is
    /// only necessary for a session opened without `persistent`.
    ///
    /// Raises `TTError` with:
    ///
    /// - `ENODEV` if the device could not be reopened.
    /// - `EAGAIN` if the session was opened with `nonblocking` and another
    ///   client holds the device incompatibly.
    pub fn reopen(&mut self) -> PyResult<()> {
        // SAFETY: `self.raw` is an initialized session. tt_reopen closes the
        // stale descriptor and reopens the device with the session's flags.
        crate::err::check(unsafe { ffi::tt_reopen(self.as_mut_ptr()) })
    }

    /// Returns the underlying device descriptor.
    pub fn dev(&self) -> Device {
        Device(self.raw.dev)
    }

    /// Returns static information about the device.
    ///
    /// Raises `TTError` with:
    ///
    /// - `ENOTCONN` if the session has been closed.
    /// - `ECONNRESET` if the session was severed by an out-of-band device
    ///   reset or removal.
    ///
    /// Other `errno` values propagate from the failing system call.
    pub fn info(&mut self) -> PyResult<Info> {
        self.perform(|sess| {
            // SAFETY: Zeroing tt_dev_info_t is valid. All fields are plain
            // integers.
            let mut info: ffi::tt_dev_info_t = unsafe { std::mem::zeroed() };
            info.output_size_bytes = std::mem::size_of::<ffi::tt_dev_info_t>() as u32;
            // SAFETY: `sess` is an open session and info is a valid
            // out-pointer.
            crate::err::check(unsafe { ffi::tt_dev_info(sess, &mut info) })?;
            Ok(Info(info))
        })
    }

    fn __repr__(&self) -> String {
        if self.is_closed() {
            "Session(closed)".to_string()
        } else {
            format!("Session(id={})", self.raw.dev.id)
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
