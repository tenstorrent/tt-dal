//! Version conversion.

use std::ffi::{CStr, c_char};

use semver::{BuildMetadata, Prerelease, Version};

use crate::ffi;

/// Converts a raw [`ttdal_sys::tt_version_t`] into a [`Version`].
pub(crate) fn from_raw(vers: ffi::tt_version_t) -> Version {
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
