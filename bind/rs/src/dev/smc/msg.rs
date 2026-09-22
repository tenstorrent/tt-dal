// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

//! SMC messages.

use std::io;
use std::time::Duration;

use crate::dev::Session;
use crate::ffi;
use crate::{Result, err};

/// SMC message.
#[derive(Clone, Copy, Debug, Default)]
pub struct Message {
    /// Message words. Word 0 is the header (message code in the low byte plus
    /// any per-message packed fields, and the firmware status on a response);
    /// words 1 through 7 carry the request arguments or the response data.
    pub message: [u32; 8],
}

/// A posted message awaiting its reply.
///
/// Returned by [`Session::post()`]. Resolve it with [`wait()`](Self::wait) or
/// [`try_wait()`](Self::try_wait). Dropping it drops the message, which frees
/// the session to send again.
///
/// Dropping does not reliably stop the message. It is discarded outright only
/// while it is still queued behind another client's message. Once it reaches
/// the controller, which is usually before [`Session::post()`] returns, the
/// message runs and only its reply is thrown away. Treat a dropped message as
/// one that may have run.
#[must_use = "dropping a queued message drops it without reading the reply"]
pub struct Queued<'sess> {
    /// Session the message was posted on.
    sess: &'sess mut Session,
    /// Whether the reply has been taken, so the drop is a no-op.
    done: bool,
}

impl<'sess> Queued<'sess> {
    /// Posts `req` on `sess` and returns the handle to its reply.
    pub(super) fn post(sess: &'sess mut Session, req: Message) -> Result<Self> {
        let req = ffi::tt_smc_msg_t {
            message: req.message,
        };

        // SAFETY: `sess` is an open device and `req` is a valid
        // `tt_smc_msg_t`.
        err::check(unsafe { ffi::tt_smc_post(sess.as_ptr(), &raw const req) })?;

        Ok(Self { sess, done: false })
    }

    /// Blocks until the reply arrives or `timeout` elapses.
    ///
    /// `None` uses the driver default. On timeout the message is left queued,
    /// to wait on again or drop. Success means the exchange completed, not
    /// that the firmware accepted the message. Inspect `message[0]` (the
    /// firmware status) for message-level errors.
    ///
    /// # Errors
    ///
    /// Returns [`Err`] if the wait times out or the exchange itself failed.
    pub fn wait(&mut self, timeout: Option<Duration>) -> Result<Message> {
        let mut rsp = ffi::tt_smc_msg_t { message: [0; 8] };

        // SAFETY: `sess` is an open device and `rsp` is a valid
        // `tt_smc_msg_t`.
        let res = err::check(unsafe {
            ffi::tt_smc_wait(self.sess.as_ptr(), &raw mut rsp, millis(timeout))
        });
        self.settle(&res, io::ErrorKind::TimedOut);
        res?;

        Ok(Message {
            message: rsp.message,
        })
    }

    /// Checks for the reply without blocking.
    ///
    /// Returns [`WouldBlock`] while the reply is pending, leaving the message
    /// queued to check again. Success means the exchange completed, not that
    /// the firmware accepted the message. Inspect `message[0]` (the firmware
    /// status) for message-level errors.
    ///
    /// [`WouldBlock`]: std::io::ErrorKind::WouldBlock
    ///
    /// # Errors
    ///
    /// Returns [`Err`] if the reply is not ready or the exchange itself
    /// failed.
    pub fn try_wait(&mut self) -> Result<Message> {
        let mut rsp = ffi::tt_smc_msg_t { message: [0; 8] };

        // SAFETY: `sess` is an open device and `rsp` is a valid
        // `tt_smc_msg_t`.
        let res = err::check(unsafe { ffi::tt_smc_poll(self.sess.as_ptr(), &raw mut rsp) });
        self.settle(&res, io::ErrorKind::WouldBlock);
        res?;

        Ok(Message {
            message: rsp.message,
        })
    }

    /// Records whether the message survived an attempt to read its reply.
    ///
    /// A success consumed it, and so did any failure other than `retry`, which
    /// is the one outcome that leaves it queued. The driver frees the slot on
    /// a failed exchange, so only `retry` needs a drop later.
    fn settle(&mut self, res: &Result<()>, retry: io::ErrorKind) {
        self.done = match res {
            Ok(()) => true,
            Err(err) => err.kind() != retry,
        };
    }
}

impl Drop for Queued<'_> {
    fn drop(&mut self) {
        if !self.done {
            // SAFETY: `sess` is an open device. The ioctl always succeeds, so
            // there is nothing to report.
            let _ = unsafe { ffi::tt_smc_drop(self.sess.as_ptr()) };
        }
    }
}

/// Converts a timeout into whole milliseconds, `0` meaning the driver default.
pub(super) fn millis(timeout: Option<Duration>) -> u32 {
    timeout.map_or(0, |d| u32::try_from(d.as_millis()).unwrap_or(u32::MAX))
}
