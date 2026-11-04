// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

//! Firmware.

use pyo3::prelude::*;

use crate::ffi;
use crate::ver::raw_to_semver;

/// Returns the firmware bundle version.
///
/// Raises `TTError` with:
///
/// - `ENOTCONN` if the session has been closed.
/// - `EIO` if the firmware version string is empty or malformed.
///
/// Other `errno` values propagate from the failing system call.
#[pyfunction]
pub fn version(sess: &crate::dev::Session, py: Python<'_>) -> PyResult<Py<PyAny>> {
    let mut raw = std::mem::MaybeUninit::<ffi::tt_version_t>::uninit();
    // SAFETY: `sess` is an open session and `raw` is a valid out-pointer.
    crate::err::check(unsafe { ffi::tt_fw_version(sess.as_ptr(), raw.as_mut_ptr()) })?;
    // SAFETY: `raw` was fully initialized by the successful call above.
    raw_to_semver(py, unsafe { raw.assume_init() })
}
