// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

//! Chip telemetry.
//!
//! Provides a snapshot of runtime sensor and status values reported by the
//! device firmware. A snapshot holds the firmware's telemetry table as read:
//! one [`Entry`] per published tag, each pairing the firmware's tag number with
//! its raw `u32` value, in the firmware's table order.
//!
//! The library neither names tags nor decodes values. The meaning and encoding
//! of each tag are defined by the firmware, documented in
//! [tt-system-firmware].
//!
//! # Usage
//!
//! Read a telemetry snapshot with [`Session::telemetry()`] and look up a value
//! by its tag number.
//!
//! ```no_run
//! # use ttdal::dev::{Device, Session};
//! #
//! # let dev = Device::scan().unwrap().next().unwrap();
//! #
//! let sess = Session::open(dev)?;
//! let telem = sess.telemetry()?;
//!
//! let aiclk = telem[14];
//! let temp = telem.get(11);
//! for entry in &telem {
//!     println!("{}: {}", entry.tag, entry.raw);
//! }
//! #
//! # Ok::<(), ttdal::Error>(())
//! ```
//!
//! [tt-system-firmware]: https://github.com/tenstorrent/tt-system-firmware

use crate::ffi;
use std::ops::Index;
use std::slice;

use super::Session;
use crate::{Error, Result};

/// Telemetry entry.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Entry {
    /// Firmware tag number.
    pub tag: u16,
    /// Raw telemetry value.
    pub raw: u32,
}

/// A snapshot of device telemetry, in the firmware's table order.
///
/// Tags the firmware does not publish are absent.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Telemetry(Vec<Entry>);

impl Telemetry {
    /// Returns the raw value for `tag`, or `None` if the tag is absent.
    #[must_use]
    pub fn get(&self, tag: u16) -> Option<u32> {
        self.0
            .iter()
            .find(|entry| entry.tag == tag)
            .map(|entry| entry.raw)
    }

    /// Returns an iterator over the entries, in the firmware's table order.
    pub fn iter(&self) -> slice::Iter<'_, Entry> {
        self.0.iter()
    }
}

impl Index<u16> for Telemetry {
    type Output = u32;

    /// Returns a reference to the raw value for `tag`.
    ///
    /// # Panics
    ///
    /// Panics if the tag is absent.
    fn index(&self, tag: u16) -> &u32 {
        self.0
            .iter()
            .find(|entry| entry.tag == tag)
            .map(|entry| &entry.raw)
            .expect("telemetry tag absent")
    }
}

impl<'a> IntoIterator for &'a Telemetry {
    type Item = &'a Entry;
    type IntoIter = slice::Iter<'a, Entry>;

    fn into_iter(self) -> Self::IntoIter {
        self.iter()
    }
}

impl Session {
    /// Reads a complete telemetry snapshot.
    ///
    /// Tags the firmware does not publish are absent.
    ///
    /// # Errors
    ///
    /// Returns [`ConnectionReset`] if the session was severed by an
    /// out-of-band device reset or removal, or [`Unsupported`] if the device
    /// architecture is unsupported. Returns `EIO`, observable via
    /// [`Error::raw_os_error()`], if the telemetry data is malformed or
    /// cannot be read. Other `errno` values propagate unchanged.
    ///
    /// [`ConnectionReset`]: std::io::ErrorKind::ConnectionReset
    /// [`Unsupported`]: std::io::ErrorKind::Unsupported
    /// [`Error::raw_os_error()`]: crate::Error::raw_os_error
    pub fn telemetry(&self) -> Result<Telemetry> {
        // Grow-and-retry until all entries fit.
        //
        // `tt_telemetry` returns the total entry count even when it exceeds
        // `cap`. If that happens, set the new cap to twice the observed count
        // and retry.
        let mut cap = 128usize;
        let mut buf = Vec::<ffi::tt_telemetry_t>::with_capacity(cap);

        // Iterate until all entries read
        let len = loop {
            // Ensure buffer allocation is large enough
            buf.reserve(cap);

            // SAFETY: `self` is an open device, and `buf` has capacity for at
            // least `cap` elements, so `buf.as_mut_ptr()` is valid for writes
            // of `cap` `tt_telemetry_t`s.
            let found = unsafe { ffi::tt_telemetry(self.as_ptr(), cap, buf.as_mut_ptr()) };

            // Check if capacity was large enough for the table
            match usize::try_from(found) {
                // Read failed, report error.
                Err(_) => return Err(Error::last_error()),
                // All entries fit in buffer.
                Ok(len) if len <= cap => break len,
                // Too small, grow and retry.
                Ok(len) => cap = len * 2,
            }
        };

        // SAFETY: `len` holds the exact number of entries initialized by
        // `tt_telemetry` and does not exceed the allocated capacity.
        unsafe { buf.set_len(len) };

        // Convert each raw entry
        let entries = buf
            .into_iter()
            .map(|entry| Entry {
                tag: entry.tag,
                raw: entry.raw,
            })
            .collect();
        Ok(Telemetry(entries))
    }
}

#[cfg(test)]
mod tests {
    use serial_test::serial;

    use super::*;

    /// Returns two entries, out of tag order.
    fn sample() -> Telemetry {
        Telemetry(vec![Entry { tag: 32, raw: 7 }, Entry { tag: 14, raw: 800 }])
    }

    #[test]
    fn telemetry_get() {
        let telem = sample();
        assert_eq!(telem.get(14), Some(800));
        assert_eq!(telem.get(1), None);
    }

    #[test]
    fn telemetry_index() {
        assert_eq!(sample()[32], 7);
    }

    #[test]
    #[should_panic(expected = "telemetry tag absent")]
    fn telemetry_index_absent() {
        let _ = sample()[1];
    }

    #[test]
    fn telemetry_iter() {
        let tags: Vec<_> = sample().iter().map(|entry| entry.tag).collect();
        assert_eq!(tags, [32, 14]);
    }

    #[test]
    #[ignore = "requires hardware"]
    #[serial]
    fn telemetry_smoke() {
        let telem = crate::tests::open().telemetry().expect("telemetry failed");
        // Firmware heartbeat is tag 32
        assert!(telem.get(32).is_some_and(|beat| beat != 0));
    }
}
