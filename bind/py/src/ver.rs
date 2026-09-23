// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

//! Version conversion.

use crate::ffi;
use std::ffi::CStr;
use std::os::raw::c_char;

use pyo3::prelude::*;

pub(crate) fn raw_to_semver(py: Python<'_>, vers: ffi::tt_version_t) -> PyResult<Py<PyAny>> {
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
