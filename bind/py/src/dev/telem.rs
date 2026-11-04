// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

//! Chip telemetry.
//!
//! Provides a snapshot of runtime sensor and status values reported by the
//! device firmware. A snapshot reads like a read-only mapping from each
//! published tag number to its raw `u32` value, in the firmware's table order,
//! and compares equal to any mapping with the same items, as a `dict` does.
//!
//! The library neither names tags nor decodes values. The meaning and encoding
//! of each tag are defined by the firmware, documented in
//! [tt-system-firmware].
//!
//! [tt-system-firmware]: https://github.com/tenstorrent/tt-system-firmware

use crate::ffi;
use pyo3::exceptions::PyKeyError;
use pyo3::prelude::*;
use pyo3::types::{IntoPyDict, PyBool, PyDict};

/// A snapshot of device telemetry, in the firmware's table order.
///
/// Reads like a read-only mapping from tag number to raw value, and is
/// registered as a `collections.abc.Mapping`. Tags the firmware does not
/// publish are absent. `keys()`, `values()` and `items()` return the views from
/// `collections.abc`.
///
/// Compares equal to any mapping with the same items, in any order, as a `dict`
/// does. Like a `dict`, it is unhashable.
#[pyclass(frozen, mapping)]
pub struct Telemetry {
    entries: Vec<(u16, u32)>,
}

#[pymethods]
impl Telemetry {
    /// Returns the raw value for `tag`, or `default` if the tag is absent.
    #[pyo3(signature = (tag, default=None))]
    fn get<'py>(
        &self,
        tag: &Bound<'py, PyAny>,
        default: Option<Bound<'py, PyAny>>,
    ) -> PyResult<Option<Bound<'py, PyAny>>> {
        let Some(raw) = self.lookup(tag)? else {
            return Ok(default);
        };
        Ok(Some(raw.into_pyobject(tag.py())?.into_any()))
    }

    /// Returns the raw value for `tag`.
    ///
    /// Raises `KeyError` if the tag is absent.
    fn __getitem__(&self, tag: &Bound<'_, PyAny>) -> PyResult<u32> {
        self.lookup(tag)?
            .ok_or_else(|| PyKeyError::new_err((tag.clone().unbind(),)))
    }

    /// Returns whether `tag` is present.
    fn __contains__(&self, tag: &Bound<'_, PyAny>) -> PyResult<bool> {
        Ok(self.lookup(tag)?.is_some())
    }

    /// Returns the number of tags.
    fn __len__(&self) -> usize {
        self.entries.len()
    }

    /// Iterates the tags, in the firmware's table order.
    fn __iter__<'py>(&self, py: Python<'py>) -> PyResult<Bound<'py, PyAny>> {
        iterate(py, self.entries.iter().map(|(tag, _)| *tag))
    }

    /// Iterates the tags, in reverse table order.
    fn __reversed__<'py>(&self, py: Python<'py>) -> PyResult<Bound<'py, PyAny>> {
        iterate(py, self.entries.iter().rev().map(|(tag, _)| *tag))
    }

    /// Returns a view of the tags, in the firmware's table order.
    fn keys<'py>(slf: &Bound<'py, Self>) -> PyResult<Bound<'py, PyAny>> {
        view(slf, "KeysView")
    }

    /// Returns a view of the raw values, in the firmware's table order.
    fn values<'py>(slf: &Bound<'py, Self>) -> PyResult<Bound<'py, PyAny>> {
        view(slf, "ValuesView")
    }

    /// Returns a view of the `(tag, raw)` pairs, in the firmware's table order.
    fn items<'py>(slf: &Bound<'py, Self>) -> PyResult<Bound<'py, PyAny>> {
        view(slf, "ItemsView")
    }

    /// Returns whether `other` is a mapping with the same items.
    ///
    /// Returns `NotImplemented` if `other` is not a mapping.
    fn __eq__(&self, other: &Bound<'_, PyAny>) -> PyResult<Py<PyAny>> {
        let eq = self.compare(other)?;
        Ok(verdict(other.py(), eq))
    }

    /// Returns whether `other` is a mapping with different items.
    ///
    /// Returns `NotImplemented` if `other` is not a mapping.
    fn __ne__(&self, other: &Bound<'_, PyAny>) -> PyResult<Py<PyAny>> {
        let ne = self.compare(other)?.map(|eq| !eq);
        Ok(verdict(other.py(), ne))
    }

    fn __repr__(&self) -> String {
        format!("Telemetry([{} entries])", self.entries.len())
    }
}

impl Telemetry {
    /// Returns the raw value for `tag`, if `tag` is a present tag number.
    ///
    /// Matches keys as a `dict` does, so `1.0` finds tag `1`, and raises
    /// `TypeError` if `tag` is unhashable.
    fn lookup(&self, tag: &Bound<'_, PyAny>) -> PyResult<Option<u32>> {
        let Ok(key) = u16::try_from(tag.hash()?) else {
            return Ok(None);
        };
        let found = self.entries.iter().find(|(entry, _)| *entry == key);
        let Some(&(_, raw)) = found else {
            return Ok(None);
        };
        Ok(tag.eq(key)?.then_some(raw))
    }

    /// Returns whether `other` is a mapping with the same items, or `None` if
    /// `other` is not a mapping.
    ///
    /// Compares as `collections.abc.Mapping` does, by building a `dict` from
    /// each side's items.
    fn compare(&self, other: &Bound<'_, PyAny>) -> PyResult<Option<bool>> {
        let py = other.py();
        let mapping = py.import("collections.abc")?.getattr("Mapping")?;
        if !other.is_instance(&mapping)? {
            return Ok(None);
        }
        let lhs = self.entries.iter().copied().into_py_dict(py)?;
        let items = other.call_method0("items")?;
        let rhs = py.get_type::<PyDict>().call1((items,))?;
        lhs.eq(rhs).map(Some)
    }
}

/// Returns a Python iterator over `tags`.
fn iterate(py: Python<'_>, tags: impl Iterator<Item = u16>) -> PyResult<Bound<'_, PyAny>> {
    let tags: Vec<u16> = tags.collect();
    tags.into_pyobject(py)?.try_iter().map(Bound::into_any)
}

/// Returns the `collections.abc` view named `kind` over `slf`.
fn view<'py>(slf: &Bound<'py, Telemetry>, kind: &str) -> PyResult<Bound<'py, PyAny>> {
    slf.py()
        .import("collections.abc")?
        .getattr(kind)?
        .call1((slf,))
}

/// Converts a comparison result to Python, with `None` as `NotImplemented`.
fn verdict(py: Python<'_>, res: Option<bool>) -> Py<PyAny> {
    match res {
        Some(res) => PyBool::new(py, res).to_owned().into_any().unbind(),
        None => py.NotImplemented(),
    }
}

use super::Session;

#[pymethods]
impl Session {
    /// Reads a complete telemetry snapshot.
    ///
    /// Tags the firmware does not publish are absent.
    ///
    /// Raises `TTError` with:
    ///
    /// - `ENOTCONN` if the session has been closed.
    /// - `ECONNRESET` if the session was severed by an out-of-band device
    ///   reset or removal.
    /// - `EIO` if the telemetry data is malformed or cannot be read.
    /// - `ENOTSUP` if the device architecture is unsupported.
    ///
    /// Other `errno` values propagate from the failing system call.
    pub fn telemetry(&self) -> PyResult<Telemetry> {
        // Grow-and-retry until all entries fit. `tt_telemetry` returns the
        // total count even when it exceeds cap, so retry with twice that.
        let mut cap = 128usize;
        let mut buf: Vec<ffi::tt_telemetry_t> = Vec::new();

        let len = loop {
            buf.reserve(cap);
            // SAFETY: `self` is an open device and `buf` has capacity for at
            // least cap elements.
            let found = unsafe { ffi::tt_telemetry(self.as_ptr(), cap, buf.as_mut_ptr()) };
            match usize::try_from(found) {
                Err(_) => return Err(crate::err::check(-1).unwrap_err()),
                Ok(len) if len <= cap => break len,
                Ok(len) => cap = len * 2,
            }
        };

        // SAFETY: `len` elements were initialized by tt_telemetry.
        unsafe { buf.set_len(len) };
        let entries = buf.into_iter().map(|entry| (entry.tag, entry.raw));
        Ok(Telemetry {
            entries: entries.collect(),
        })
    }
}
