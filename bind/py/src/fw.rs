//! Firmware.

use pyo3::prelude::*;

use crate::ffi;
use crate::ver::raw_to_semver;

/// Returns firmware bundle version.
///
/// Raises `TTError` with:
///
/// - `ENOTCONN` if the session has been closed.
/// - `EIO` if the firmware version string is empty or malformed.
///
/// Other `errno` values propagate from the failing system call.
#[pyfunction]
pub fn version(sess: &mut crate::dev::Session, py: Python<'_>) -> PyResult<Py<PyAny>> {
    let vers = sess.call(|sess| {
        let mut raw = std::mem::MaybeUninit::<ffi::tt_version_t>::uninit();
        // SAFETY: `sess` is an open device and `raw` is a valid out-pointer.
        crate::err::check(unsafe { ffi::tt_fw_version(sess, raw.as_mut_ptr()) })?;
        // SAFETY: `raw` was fully initialized by the successful call above.
        Ok(unsafe { raw.assume_init() })
    })?;
    raw_to_semver(py, vers)
}
