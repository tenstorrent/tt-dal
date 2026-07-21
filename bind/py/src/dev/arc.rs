//! ARC controller.
//!
//! The ARC is an on-chip firmware controller responsible for power management,
//! clock configuration, and other low-level device operations. Communication
//! with ARC uses a `Message`-based request-response interface.

use crate::ffi;
use pyo3::prelude::*;

/// An ARC controller message.
///
/// Contains a message code (first byte of `data[0]`) and up to 8 u32 data
/// words. The response is returned in-place by `Session.message()`.
#[pyclass(from_py_object)]
#[derive(Clone)]
pub struct Message(pub(crate) ffi::tt_message_t);

#[pymethods]
impl Message {
    /// Creates an ARC message with the given code and data words.
    ///
    /// Raises `ValueError` if `data` has more than 8 elements.
    #[new]
    #[pyo3(signature = (code=0, data=None))]
    fn new(code: u8, data: Option<Vec<u32>>) -> PyResult<Self> {
        let mut raw: ffi::tt_message_t = unsafe { std::mem::zeroed() };
        raw.code = code;
        if let Some(words) = data {
            if words.len() > 8 {
                return Err(pyo3::exceptions::PyValueError::new_err(
                    "data must have at most 8 elements",
                ));
            }
            for (i, w) in words.into_iter().enumerate() {
                raw.data[i] = w;
            }
        }
        Ok(Self(raw))
    }

    #[getter]
    fn code(&self) -> u8 {
        self.0.code
    }

    #[setter]
    fn set_code(&mut self, code: u8) {
        self.0.code = code;
    }

    #[getter]
    fn data(&self) -> Vec<u32> {
        self.0.data.to_vec()
    }

    /// Sets the message data words.
    ///
    /// Raises `ValueError` if `data` has more than 8 elements.
    #[setter]
    fn set_data(&mut self, data: Vec<u32>) -> PyResult<()> {
        if data.len() > 8 {
            return Err(pyo3::exceptions::PyValueError::new_err(
                "data must have at most 8 elements",
            ));
        }
        self.0.data = [0u32; 8];
        for (i, w) in data.into_iter().enumerate() {
            self.0.data[i] = w;
        }
        Ok(())
    }

    fn __repr__(&self) -> String {
        format!("Message(code={:#04x}, data={:?})", self.0.code, self.0.data)
    }
}

use super::Session;

#[pymethods]
impl Session {
    /// Sends a message to the ARC controller and returns the response.
    ///
    /// Set `wait` to block until the controller responds. `timeout` of `None`
    /// uses the driver default.
    ///
    /// Raises `TTError` with `ENOTCONN` if the session has been closed.
    /// ARC messaging is otherwise not yet implemented, so the underlying
    /// call aborts before returning.
    #[pyo3(signature = (msg, wait=true, timeout=None))]
    pub fn message(&self, msg: Message, wait: bool, timeout: Option<u32>) -> PyResult<Message> {
        let mut raw = msg.0;
        // SAFETY: Self is an open device and raw is a valid tt_message_t.
        crate::err::check(unsafe {
            ffi::tt_message(Session::as_ptr(self), &mut raw, wait, timeout.unwrap_or(0))
        })?;
        Ok(Message(raw))
    }
}
