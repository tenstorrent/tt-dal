// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

//! Kernel driver.

use semver::Version;

use crate::ver::from_raw;
use crate::{Result, err, ffi};

/// Returns kernel driver version.
///
/// Discovers and briefly opens an available device to issue the query.
///
/// # Errors
///
/// Returns [`ConnectionReset`] if the device was reset or removed while
/// querying. Returns `ENODEV`, observable via [`Error::raw_os_error()`], if
/// no device is available. Other `errno` values from the failing `ioctl`
/// propagate unchanged.
///
/// [`ConnectionReset`]: std::io::ErrorKind::ConnectionReset
/// [`Error::raw_os_error()`]: crate::Error::raw_os_error
pub fn version() -> Result<Version> {
    let mut raw = std::mem::MaybeUninit::<ffi::tt_version_t>::uninit();
    // SAFETY: `raw` is a valid out-pointer for `tt_version_t`.
    err::check(unsafe { ffi::tt_kmd_version(raw.as_mut_ptr()) })?;
    // SAFETY: `raw` was fully initialized by the successful call above.
    Ok(from_raw(unsafe { raw.assume_init() }))
}

#[cfg(test)]
mod tests {
    use serial_test::serial;

    use super::*;

    #[test]
    #[ignore = "requires tt-kmd"]
    #[serial]
    fn kmd_version() {
        assert!(Version::new(2, 7, 0) <= version().expect("kmd version failed"));
    }
}
