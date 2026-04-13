//! Error reporting.

use crate::ffi;
use std::ffi::CStr;

use pyo3::create_exception;
use pyo3::prelude::*;

create_exception!(ttdal, Error, pyo3::exceptions::PyException);

/// Converts a C-style `tt-dal` return code into a `PyResult`.
pub(crate) fn check(ret: core::ffi::c_int) -> PyResult<()> {
    if ret == 0 {
        Ok(())
    } else {
        // SAFETY: `tt_get_errno` reads the thread-local `tt_errno` set by the
        // most recent failing `tt-dal` call on this thread.
        let errno = unsafe { ffi::tt_get_errno() };
        // SAFETY: `tt_error_describe` returns a pointer to a static C string
        // literal or NULL. If non-null, it is always null-terminated.
        let ptr = unsafe { ffi::tt_error_describe(errno) };
        let msg = if !ptr.is_null() {
            unsafe { CStr::from_ptr(ptr) }
                .to_string_lossy()
                .into_owned()
        } else {
            format!("tt-dal error {errno}")
        };
        Err(Error::new_err(msg))
    }
}
