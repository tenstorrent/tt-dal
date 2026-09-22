// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

//! System Management Controller.
//!
//! The System Management Controller (SMC) is an on-chip firmware controller
//! responsible for power management, clock configuration, and other
//! low-level device operations. Communication with the SMC uses a
//! `Message`-based request-response interface.
//!
//! `Session.call()` performs a whole exchange. `Session.post()` returns a
//! `Queued` handle for driving one by hand.

use crate::ffi;
use pyo3::prelude::*;

/// An SMC controller message.
///
/// Holds the eight-word SMC payload. Word 0 is the header (message code in the
/// low byte plus any per-message packed fields, and the firmware status on a
/// response); words 1 through 7 carry the request arguments or the response
/// data. `Session.call()` returns the reply as a new `Message`.
#[pyclass(from_py_object)]
#[derive(Clone)]
pub struct Message(pub(crate) ffi::tt_smc_msg_t);

#[pymethods]
impl Message {
    /// Creates an SMC message from up to 8 payload words.
    ///
    /// Raises `ValueError` if `message` has more than 8 elements.
    #[new]
    #[pyo3(signature = (message=None))]
    fn new(message: Option<Vec<u32>>) -> PyResult<Self> {
        let mut raw: ffi::tt_smc_msg_t = unsafe { std::mem::zeroed() };
        if let Some(words) = message {
            if words.len() > 8 {
                return Err(pyo3::exceptions::PyValueError::new_err(
                    "message must have at most 8 elements",
                ));
            }
            for (i, w) in words.into_iter().enumerate() {
                raw.message[i] = w;
            }
        }
        Ok(Self(raw))
    }

    #[getter]
    fn message(&self) -> Vec<u32> {
        self.0.message.to_vec()
    }

    /// Sets the message words.
    ///
    /// Raises `ValueError` if `message` has more than 8 elements.
    #[setter]
    fn set_message(&mut self, message: Vec<u32>) -> PyResult<()> {
        if message.len() > 8 {
            return Err(pyo3::exceptions::PyValueError::new_err(
                "message must have at most 8 elements",
            ));
        }
        self.0.message = [0u32; 8];
        for (i, w) in message.into_iter().enumerate() {
            self.0.message[i] = w;
        }
        Ok(())
    }

    fn __repr__(&self) -> String {
        format!("Message(message={:?})", self.0.message)
    }
}

use super::Session;

/// A posted SMC message awaiting its reply.
///
/// Returned by `Session.post()`. Use it as a context manager so the message is
/// dropped on block exit:
///
///     with sess.post(req) as pending:
///         rsp = pending.poll()
///
/// The message is also dropped when the handle is garbage collected, but
/// relying on that leaves it outstanding for an unpredictable length of time.
#[pyclass]
pub struct Queued {
    /// Session the message was posted on, kept alive for the handle.
    sess: Py<Session>,
    /// Whether the reply has been taken, so the drop is a no-op.
    done: bool,
}

impl Queued {
    /// Discards the message, ignoring errors.
    fn discard(&mut self, py: Python<'_>) {
        if self.done {
            return;
        }
        self.done = true;
        if let Ok(sess) = self.sess.try_borrow(py) {
            // SAFETY: `sess` outlives this call and the ioctl only reads it.
            let _ = unsafe { ffi::tt_smc_drop(Session::as_ptr(&sess)) };
        }
    }
}

impl Drop for Queued {
    fn drop(&mut self) {
        let _ = Python::try_attach(|py| self.discard(py));
    }
}

#[pymethods]
impl Queued {
    /// Checks for the reply without blocking.
    ///
    /// Returns `None` while the reply is pending, leaving the message
    /// outstanding to poll again. Success means the exchange completed, not
    /// that the firmware accepted the message. Inspect `message[0]` (the
    /// firmware status) for message-level errors.
    ///
    /// Raises `TTError` with `ENOTCONN` if the session has been closed, or
    /// carrying the `errno` of a failed exchange.
    pub fn poll(&mut self, py: Python<'_>) -> PyResult<Option<Message>> {
        let sess = self.sess.try_borrow(py)?;
        let mut rsp: ffi::tt_smc_msg_t = unsafe { std::mem::zeroed() };
        // SAFETY: `sess` is a live session and rsp is a valid tt_smc_msg_t.
        if unsafe { ffi::tt_smc_poll(Session::as_ptr(&sess), &raw mut rsp) } != 0 {
            let errno = std::io::Error::last_os_error().raw_os_error().unwrap_or(0);
            // A pending reply is not a failure.
            if errno == libc::EAGAIN {
                return Ok(None);
            }
            // Any other error consumed the message in the driver.
            self.done = true;
            return Err(crate::err::fail(errno));
        }
        self.done = true;
        Ok(Some(Message(rsp)))
    }

    /// Discards the message.
    ///
    /// Idempotent, and a no-op once the reply has been taken.
    ///
    /// This does not reliably stop the message. It is discarded outright only
    /// while it is still queued behind another client's message. Once it
    /// reaches the controller, which is usually before `post()` returns, the
    /// message runs and only its reply is thrown away. Treat a dropped message
    /// as one that may have run.
    pub fn drop(&mut self, py: Python<'_>) {
        self.discard(py);
    }

    fn __enter__(slf: PyRef<'_, Self>) -> PyRef<'_, Self> {
        slf
    }

    fn __exit__(
        &mut self,
        py: Python<'_>,
        _exc_type: &Bound<'_, PyAny>,
        _exc_val: &Bound<'_, PyAny>,
        _exc_tb: &Bound<'_, PyAny>,
    ) -> bool {
        self.discard(py);
        false
    }

    fn __repr__(&self) -> String {
        format!("Queued(done={})", self.done)
    }
}

#[pymethods]
impl Session {
    /// Calls the SMC controller with a message and returns the reply.
    ///
    /// Posts the message, then blocks until the reply arrives or `timeout`
    /// seconds elapse. `None` or `0` uses the driver default. Success means
    /// the exchange completed, not that the firmware accepted the message.
    /// Inspect `message[0]` (the firmware status) for message-level errors.
    /// The message is dropped on any early return, leaving the session free to
    /// send again.
    ///
    /// This releases the interpreter lock while it waits, so other threads run
    /// during the exchange. A persistent session does not reopen for messaging
    /// the way other calls do, since reopening would discard the message.
    ///
    /// Raises `TTError` with `ENOTCONN` if the session has been closed, or with
    /// `ETIMEDOUT`, `EBUSY`, or `ENOTSUP` per the underlying call.
    #[pyo3(signature = (req, timeout=None))]
    pub fn call(&self, py: Python<'_>, req: Message, timeout: Option<f64>) -> PyResult<Message> {
        let ms = millis(timeout)?;
        // Copied so the wait can run without the interpreter lock. Both are
        // plain data, and the call only reads the session.
        let sess = self.raw();
        let req = req.0;
        let (rc, errno, rsp) = py.detach(move || {
            let mut rsp: ffi::tt_smc_msg_t = unsafe { std::mem::zeroed() };
            // SAFETY: `sess` and `req` are valid for the duration of the call.
            let rc = unsafe { ffi::tt_smc_call(&raw const sess, &raw const req, &raw mut rsp, ms) };
            // Read here, before reattaching can clobber it.
            let errno = std::io::Error::last_os_error().raw_os_error().unwrap_or(0);
            (rc, errno, rsp)
        });

        if rc != 0 {
            return Err(crate::err::fail(errno));
        }
        Ok(Message(rsp))
    }

    /// Posts a message to the SMC controller without waiting.
    ///
    /// Returns a `Queued` handle for the reply. A session holds at most one
    /// message outstanding at a time.
    ///
    /// Raises `TTError` with `ENOTCONN` if the session has been closed, or with
    /// `EBUSY` or `ENOTSUP` per the underlying call.
    pub fn post(slf: Py<Self>, py: Python<'_>, req: Message) -> PyResult<Queued> {
        {
            let sess = slf.try_borrow(py)?;
            let req = req.0;
            // SAFETY: `sess` is a live session and req is a valid
            // tt_smc_msg_t.
            crate::err::check(unsafe { ffi::tt_smc_post(Session::as_ptr(&sess), &raw const req) })?;
        }
        Ok(Queued {
            sess: slf,
            done: false,
        })
    }
}

/// Converts a timeout in seconds into whole milliseconds.
///
/// `None` and `0` both select the driver default.
fn millis(timeout: Option<f64>) -> PyResult<u32> {
    let Some(secs) = timeout else { return Ok(0) };
    if !secs.is_finite() || secs < 0.0 {
        return Err(pyo3::exceptions::PyValueError::new_err(
            "timeout must be a non-negative number of seconds",
        ));
    }
    Ok((secs * 1000.0).round().min(f64::from(u32::MAX)) as u32)
}
