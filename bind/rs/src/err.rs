// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

//! Error reporting.

use std::fmt::{self, Display};
use std::io;

/// A convenient type alias for [`Result`](std::result::Result).
pub type Result<T, E = Error> = std::result::Result<T, E>;

/// An error returned by a `tt-dal` operation.
///
/// A thin wrapper over [`std::io::Error`] carrying the `errno` left behind
/// by the failing `tt-dal` call. Match on [`kind()`] to branch on the
/// failure reason.
///
/// [`kind()`]: Self::kind
#[derive(Debug)]
pub struct Error(pub(crate) io::Error);

impl Error {
    /// Returns whether the error reports a reset device connection.
    pub(crate) fn is_lost(&self) -> bool {
        self.kind() == io::ErrorKind::ConnectionReset
    }

    /// Returns an error representing the last `tt-dal` error which occurred.
    ///
    /// This function reads the thread-local `errno`. This should be called
    /// immediately after a call to a `tt-dal` function, otherwise the state
    /// of the error value is indeterminate.
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
        Self(io::Error::last_os_error())
    }

    /// Returns the corresponding [`io::ErrorKind`] for this error.
    #[must_use]
    pub fn kind(&self) -> io::ErrorKind {
        self.0.kind()
    }

    /// Returns the OS error that this error represents.
    #[must_use]
    pub fn raw_os_error(&self) -> Option<i32> {
        self.0.raw_os_error()
    }
}

impl Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        Display::fmt(&self.0, f)
    }
}

impl std::error::Error for Error {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        self.0.source()
    }
}

impl From<Error> for io::Error {
    fn from(err: Error) -> Self {
        err.0
    }
}

/// Converts a C-style `tt-dal` return code into a [`Result`].
///
/// Most `tt-dal` functions signal failure by returning
/// [`TT_ERR`](`ttdal_sys::TT_ERR`) and leaving the cause in `errno`. This
/// function checks the return code and, on failure, reads `errno` to
/// construct an [`Error`].
pub(crate) fn check(ret: core::ffi::c_int) -> Result<()> {
    if ret == 0 {
        Ok(())
    } else {
        Err(Error::last_error())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn check_zero() {
        assert!(check(0).is_ok());
    }

    #[test]
    fn check_nonzero() {
        assert!(check(-1).is_err());
    }
}
