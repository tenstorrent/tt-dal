// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

//! Kernel driver.

use semver::Version;

use crate::ver::from_raw;
use crate::{Result, err, ffi};

/// Returns the kernel driver version.
///
/// Reads the version the kernel publishes for the loaded module, so it opens no
/// device, and succeeds while every device is held or none is connected.
///
/// # Errors
///
/// Returns `ENODEV`, observable via [`Error::raw_os_error()`], if the driver is
/// not loaded, or `EIO` if the version string is empty or malformed. Other
/// `errno` values from the failing system call propagate unchanged.
///
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
