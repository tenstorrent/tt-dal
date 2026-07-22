//! SMC controller.
//!
//! The System Management Controller (SMC) is an on-chip firmware controller
//! responsible for power management, clock configuration, and other
//! low-level device operations. Communication with the SMC uses a
//! [`Message`]-based request-response interface.
//!
//! # Usage
//!
//! Send a [`Message`] to the device with [`Session::message()`].
//!
//! ```no_run
//! # use ttdal::dev::{Device, Session, smc::Message};
//! #
//! # let dev = Device::scan().unwrap().next().unwrap();
//! #
//! // Open a device session
//! let con = Session::open(dev)?;
//!
//! // Send a message, print the response
//! # let msg = Message::default();
//! # let (wait, timeout) = (true, None);
//! let rsp = con.message(msg, wait, timeout)?;
//! println!("response code: {:#x}", rsp.code);
//! #
//! # Ok::<(), ttdal::Error>(())
//! ```

use crate::ffi;
use std::time::Duration;

use super::Session;
use crate::{Result, err};

/// SMC message.
#[derive(Clone, Copy, Debug, Default)]
pub struct Message {
    /// Message code.
    pub code: u8,
    /// Message data.
    pub data: [u32; 8],
}

impl Session {
    /// Sends a message to the SMC controller and returns the response.
    ///
    /// Set `wait` to block until the controller responds. `timeout` of `None`
    /// uses the driver default.
    ///
    /// # Errors
    ///
    /// SMC messaging is not yet implemented, so the underlying call aborts
    /// before returning.
    pub fn message(&self, msg: Message, wait: bool, timeout: Option<Duration>) -> Result<Message> {
        let timeout = timeout.map_or(0, |d| u32::try_from(d.as_millis()).unwrap_or(u32::MAX));
        self.call(|sess| {
            let mut raw = ffi::tt_message_t {
                code: msg.code,
                data: msg.data,
            };

            // SAFETY: `sess` is an open device and `raw` is a valid
            // `tt_message_t`.
            err::check(unsafe { ffi::tt_message(sess.as_ptr(), &raw mut raw, wait, timeout) })?;

            Ok(Message {
                code: raw.code,
                data: raw.data,
            })
        })
    }
}
