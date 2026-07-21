//! Error reporting.

use std::ffi::CStr;

use pyo3::create_exception;
use pyo3::exceptions::PyOSError;
use pyo3::prelude::*;

create_exception!(ttdal, TTError, PyOSError);

/// Returns whether the last `tt-dal` failure reports a reset device
/// connection.
pub(crate) fn lost() -> bool {
    std::io::Error::last_os_error().raw_os_error() == Some(libc::ECONNRESET)
}

/// Builds a `TTError` for the given `errno` value.
pub(crate) fn fail(errno: core::ffi::c_int) -> PyErr {
    // SAFETY: `strerror` returns a pointer to a static, null-terminated
    // string for any value.
    let msg = unsafe { CStr::from_ptr(libc::strerror(errno)) }
        .to_string_lossy()
        .into_owned();
    TTError::new_err((errno, msg))
}

/// Converts a C-style `tt-dal` return code into a `PyResult`.
///
/// On failure, raises a `TTError` carrying the `errno` left behind by
/// the failing call and its `strerror()` description.
pub(crate) fn check(ret: core::ffi::c_int) -> PyResult<()> {
    if ret == 0 {
        return Ok(());
    }

    // Read the errno left behind by the failing call
    let errno = std::io::Error::last_os_error().raw_os_error().unwrap_or(0);
    Err(fail(errno))
}
