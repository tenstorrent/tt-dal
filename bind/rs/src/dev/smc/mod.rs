// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

//! SMC controller.
//!
//! The System Management Controller (SMC) is an on-chip firmware controller
//! responsible for power management, clock configuration, and other
//! low-level device operations. Communication with the SMC uses a
//! [`Message`]-based request-response interface.
//!
//! # Usage
//!
//! [`Session::call()`] performs a whole exchange. [`Session::post()`] returns
//! a [`Queued`] handle for driving one by hand.
//!
//! ```no_run
//! # use ttdal::dev::{Device, Session, smc::Message};
//! #
//! # let dev = Device::scan().unwrap().next().unwrap();
//! #
//! // Open a device session
//! let mut con = Session::open(dev)?;
//!
//! // Send a message, print the firmware status word
//! # let req = Message::default();
//! # let timeout = None;
//! let rsp = con.call(req, timeout)?;
//! println!("status: {:#x}", rsp.message[0]);
//! #
//! # Ok::<(), ttdal::Error>(())
//! ```

pub mod msg;

use std::time::Duration;

#[doc(inline)]
pub use self::msg::{Message, Queued};

use super::Session;
use crate::ffi;
use crate::{Result, err};

impl Session {
    /// Calls the SMC controller with a message and returns the reply.
    ///
    /// Posts the message, then blocks until the reply arrives or `timeout`
    /// elapses. `None` uses the driver default. Success means the exchange
    /// completed, not that the firmware accepted the message. Inspect
    /// `message[0]` (the firmware status) for message-level errors. The
    /// message is dropped on any early return, leaving the session free to
    /// send again.
    ///
    /// # Errors
    ///
    /// Returns [`Err`] if the session is closed, a message is already
    /// outstanding, the wait times out, or the firmware has no message queue.
    pub fn call(&self, req: Message, timeout: Option<Duration>) -> Result<Message> {
        let timeout = msg::millis(timeout);
        self.perform(|sess| {
            let req = ffi::tt_smc_msg_t {
                message: req.message,
            };
            let mut rsp = ffi::tt_smc_msg_t { message: [0; 8] };

            // SAFETY: `sess` is an open device, and `req` and `rsp` are valid
            // `tt_smc_msg_t`s.
            err::check(unsafe {
                ffi::tt_smc_call(sess.as_ptr(), &raw const req, &raw mut rsp, timeout)
            })?;

            Ok(Message {
                message: rsp.message,
            })
        })
    }

    /// Posts a message to the SMC controller without waiting.
    ///
    /// Returns a handle to the queued message. A session holds at most one
    /// message outstanding at a time, which the borrow enforces.
    ///
    /// # Errors
    ///
    /// Returns [`Err`] if the session is closed, a message is already
    /// outstanding, or the firmware has no message queue.
    pub fn post(&mut self, req: Message) -> Result<Queued<'_>> {
        Queued::post(self, req)
    }
}
