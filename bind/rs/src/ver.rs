//! Version information.

use crate::ffi;
use std::ffi::{CStr, c_char};

use semver::{BuildMetadata, Prerelease, Version};

use crate::dev::Session;
use crate::{Result, err};

/// Converts a raw [`ttdal_sys::tt_version_t`] into a [`Version`].
fn from_raw(vers: ffi::tt_version_t) -> Version {
    /// Interprets a fixed-size `c_char` buffer as a `CStr`.
    ///
    /// Returns an empty `CStr` if no nul terminator is found within the
    /// buffer, rather than reading past the end.
    fn cstr(buf: &[c_char]) -> &CStr {
        CStr::from_bytes_until_nul(
            // SAFETY: `buf` is an inline array in `tt_version_t`. It is
            // non-null and valid for `buf.len()` reads. Both `c_char` and
            // `u8` have size 1 and alignment 1, so the cast does not change
            // the underlying bit pattern.
            unsafe { std::slice::from_raw_parts(buf.as_ptr().cast::<u8>(), buf.len()) },
        )
        .unwrap_or_default()
    }

    Version {
        major: vers.major,
        minor: vers.minor,
        patch: vers.patch,
        pre: Prerelease::new(&cstr(&vers.pre).to_string_lossy()).unwrap_or_default(),
        build: BuildMetadata::new(&cstr(&vers.build).to_string_lossy()).unwrap_or_default(),
    }
}

/// Returns library interface version.
#[must_use]
pub const fn library() -> Version {
    Version::new(
        ffi::TTDAL_VERSION_MAJOR as u64,
        ffi::TTDAL_VERSION_MINOR as u64,
        ffi::TTDAL_VERSION_PATCH as u64,
    )
}

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
pub fn driver() -> Result<Version> {
    let mut raw = std::mem::MaybeUninit::<ffi::tt_version_t>::uninit();
    // SAFETY: `raw` is a valid out-pointer for `tt_version_t`.
    err::check(unsafe { ffi::tt_version_driver(raw.as_mut_ptr()) })?;
    // SAFETY: `raw` was fully initialized by the successful call above.
    Ok(from_raw(unsafe { raw.assume_init() }))
}

/// Returns firmware bundle version.
///
/// # Errors
///
/// Returns `EIO`, observable via [`Error::raw_os_error()`], if the firmware
/// version string is empty or malformed. Other `errno` values from the
/// failing `open` or `read` propagate unchanged.
///
/// [`Error::raw_os_error()`]: crate::Error::raw_os_error
pub fn firmware(dev: &Session) -> Result<Version> {
    dev.call(|sess| {
        let mut raw = std::mem::MaybeUninit::<ffi::tt_version_t>::uninit();
        // SAFETY: `sess` is an open device and `raw` is a valid out-pointer
        // for `tt_version_t`.
        err::check(unsafe { ffi::tt_version_firmware(sess.as_ptr(), raw.as_mut_ptr()) })?;
        // SAFETY: `raw` was fully initialized by the successful call above.
        Ok(from_raw(unsafe { raw.assume_init() }))
    })
}

#[cfg(test)]
mod tests {
    use serial_test::serial;

    use super::*;

    #[test]
    fn library_matches_cargo() {
        assert_eq!(
            env!("CARGO_PKG_VERSION")
                .parse::<Version>()
                .expect("invalid package version"),
            library(),
        );
    }

    #[test]
    #[ignore]
    #[serial]
    fn driver_version() {
        assert!(Version::new(2, 7, 0) <= driver().expect("driver version failed"));
    }

    #[test]
    #[ignore]
    #[serial]
    fn firmware_version() {
        assert!(
            Version::new(19, 0, 0)
                <= firmware(&crate::tests::open()).expect("firmware version failed")
        );
    }
}
