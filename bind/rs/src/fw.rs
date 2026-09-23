// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

//! Firmware.

use semver::Version;

use crate::dev::Session;
use crate::ver::from_raw;
use crate::{Result, err, ffi};

/// Returns firmware bundle version.
///
/// # Errors
///
/// Returns `EIO`, observable via [`Error::raw_os_error()`], if the firmware
/// version string is empty or malformed. Other `errno` values from the
/// failing `open` or `read` propagate unchanged.
///
/// [`Error::raw_os_error()`]: crate::Error::raw_os_error
pub fn version(dev: &Session) -> Result<Version> {
    dev.perform(|sess| {
        let mut raw = std::mem::MaybeUninit::<ffi::tt_version_t>::uninit();
        // SAFETY: `sess` is an open device and `raw` is a valid out-pointer
        // for `tt_version_t`.
        err::check(unsafe { ffi::tt_fw_version(sess.as_ptr(), raw.as_mut_ptr()) })?;
        // SAFETY: `raw` was fully initialized by the successful call above.
        Ok(from_raw(unsafe { raw.assume_init() }))
    })
}

#[cfg(test)]
mod tests {
    use serial_test::serial;

    use super::*;

    #[test]
    #[ignore]
    #[serial]
    fn fw_version() {
        assert!(
            Version::new(19, 0, 0) <= version(&crate::tests::open()).expect("fw version failed")
        );
    }
}
