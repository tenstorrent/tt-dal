//! Error reporting.

use crate::ffi;
use std::ffi::CStr;
use std::fmt::Display;

/// A convenient type alias for [`Result`](std::result::Result).
pub type Result<T, E = Error> = std::result::Result<T, E>;

/// An error returned by a `tt-dal` operation.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Error {
    /// Upstream error number.
    errno: ffi::tt_error_t,
}

impl Error {
    /// Returns an error representing the last `tt-dal` error which occurred.
    ///
    /// This function reads the value of the thread-local `tt_errno`. This
    /// should be called immediately after a call to a `tt-dal` function,
    /// otherwise the state of the error value is indeterminate.
    ///
    /// # Examples
    ///
    /// ```
    /// use ttdal::Error;
    ///
    /// let err = Error::last_error();
    /// println!("last error: {err:?}");
    /// ```
    #[must_use]
    pub fn last_error() -> Self {
        // SAFETY: `tt_get_errno` reads the thread-local `tt_errno` set by the
        // most recent failing `tt-dal` call on this thread.
        Self::from_raw_error(unsafe { ffi::tt_get_errno() })
    }

    /// Creates an [`Error`] from a raw `tt_error_t` error code.
    ///
    /// # Examples
    ///
    /// ```
    /// use ttdal::Error;
    ///
    /// let err = Error::from_raw_error(100);
    /// println!("{err}");
    /// ```
    #[must_use]
    pub fn from_raw_error(errno: ffi::tt_error_t) -> Self {
        Self { errno }
    }

    /// Returns the raw `tt_error_t` error code associated with this error.
    ///
    /// # Examples
    ///
    /// ```
    /// use ttdal::Error;
    ///
    /// fn print_error(err: &Error) {
    ///     println!("raw error: {:?}", err.raw_error());
    /// }
    ///
    /// print_error(&Error::last_error());
    /// ```
    #[must_use]
    pub fn raw_error(&self) -> ffi::tt_error_t {
        self.errno
    }
}

impl Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        // Describe error code.
        //
        // SAFETY: `tt_error_describe` returns a pointer to a static C string
        // literal or `NULL`. Static string literals in C are always
        // null-terminated.
        let ptr = unsafe { ffi::tt_error_describe(self.errno) };

        // Get string message
        let msg = if ptr.is_null() {
            c"unknown error"
        } else {
            // SAFETY: `ptr` is non-null and points to a static null-terminated
            // C string literal whose lifetime exceeds this call.
            unsafe { CStr::from_ptr(ptr) }
        }
        // Remove invalid chars.
        //
        // Pointer is always valid at this point, but nevertheless, invalid
        // characters should be replaced.
        .to_string_lossy();

        // Render the message
        Display::fmt(&msg, f)
    }
}

impl std::error::Error for Error {}

/// Converts a C-style `tt-dal` return code into a [`Result`].
///
/// Most `tt-dal` functions signal failure by returning
/// [`TT_ERR`](`ttdal_sys::TT_ERR`) and setting the thread-local `tt_errno`. This
/// function checks the return code and, on failure, reads `tt_errno` via the
/// shim to construct an [`Error`].
pub(crate) fn check(ret: core::ffi::c_int) -> Result<()> {
    if ret == 0 {
        Ok(())
    } else {
        Err(Error::last_error())
    }
}
