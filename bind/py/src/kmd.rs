// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

//! Kernel driver.

use pyo3::prelude::*;

use crate::ffi;
use crate::ver::raw_to_semver;

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
pub fn version(py: Python<'_>) -> PyResult<Py<PyAny>> {
    let mut raw = std::mem::MaybeUninit::<ffi::tt_version_t>::uninit();
    // SAFETY: `raw` is a valid out-pointer for `tt_version_t`.
    crate::err::check(unsafe { ffi::tt_kmd_version(raw.as_mut_ptr()) })?;
    // SAFETY: `raw` was fully initialized by the successful call above.
    raw_to_semver(py, unsafe { raw.assume_init() })
}
