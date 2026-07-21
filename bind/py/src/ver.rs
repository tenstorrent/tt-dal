//! Version information.

use crate::ffi;
use std::ffi::CStr;
use std::os::raw::c_char;

use pyo3::prelude::*;

fn raw_to_semver(py: Python<'_>, vers: ffi::tt_version_t) -> PyResult<Py<PyAny>> {
    fn cstr(buf: &[c_char]) -> String {
        CStr::from_bytes_until_nul(
            // SAFETY: `buf` is an inline array in `tt_version_t`. `c_char` and
            // `u8` have the same size and alignment.
            unsafe { std::slice::from_raw_parts(buf.as_ptr() as *const u8, buf.len()) },
        )
        .unwrap_or_default()
        .to_string_lossy()
        .into_owned()
    }

    let pre = cstr(&vers.pre);
    let build = cstr(&vers.build);

    let mut s = format!("{}.{}.{}", vers.major, vers.minor, vers.patch);
    if !pre.is_empty() {
        s.push('-');
        s.push_str(&pre);
    }
    if !build.is_empty() {
        s.push('+');
        s.push_str(&build);
    }

    py.import("semver")?
        .getattr("Version")?
        .call_method1("parse", (s,))
        .map(Bound::unbind)
}

/// Returns library interface version.
#[pyfunction]
pub fn library(py: Python<'_>) -> PyResult<Py<PyAny>> {
    let s = format!(
        "{}.{}.{}",
        ffi::TT_VERSION_MAJOR,
        ffi::TT_VERSION_MINOR,
        ffi::TT_VERSION_PATCH,
    );
    py.import("semver")?
        .getattr("Version")?
        .call_method1("parse", (s,))
        .map(Bound::unbind)
}

/// Returns kernel driver version.
///
/// Discovers and briefly opens an available device to issue the query.
///
/// Raises `TTError` with:
///
/// - `ENODEV` if no device is available.
/// - `ECONNRESET` if the device was reset or removed while querying.
///
/// Other `errno` values propagate from the failing system call.
#[pyfunction]
pub fn driver(py: Python<'_>) -> PyResult<Py<PyAny>> {
    let mut raw = std::mem::MaybeUninit::<ffi::tt_version_t>::uninit();
    // SAFETY: `raw` is a valid out-pointer for `tt_version_t`.
    crate::err::check(unsafe { ffi::tt_version_driver(raw.as_mut_ptr()) })?;
    // SAFETY: `raw` was fully initialized by the successful call above.
    raw_to_semver(py, unsafe { raw.assume_init() })
}

/// Returns firmware bundle version.
///
/// Raises `TTError` with:
///
/// - `ENOTCONN` if the session has been closed.
/// - `EIO` if the firmware version string is empty or malformed.
///
/// Other `errno` values propagate from the failing system call.
#[pyfunction]
pub fn firmware(sess: &crate::dev::Session, py: Python<'_>) -> PyResult<Py<PyAny>> {
    let mut raw = std::mem::MaybeUninit::<ffi::tt_version_t>::uninit();
    // SAFETY: `sess` is an open device and `raw` is a valid out-pointer.
    crate::err::check(unsafe {
        ffi::tt_version_firmware(crate::dev::Session::as_ptr(sess), raw.as_mut_ptr())
    })?;
    // SAFETY: `raw` was fully initialized by the successful call above.
    raw_to_semver(py, unsafe { raw.assume_init() })
}
