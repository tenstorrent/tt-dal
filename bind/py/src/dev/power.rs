//! Power state.
//!
//! Controls which power features are requested to be active on the device. Each
//! `Flag` value corresponds to a single feature, enabled when present and
//! disabled when absent.
//!
//! The kernel driver aggregates power requests across all open sessions. A
//! feature stays enabled as long as any session requests it and is withdrawn
//! automatically when that session closes.

use crate::ffi;
use pyo3::prelude::*;

/// Power feature flag.
///
/// A single controllable power feature on the device. Combine flags with `|`
/// and pass to `Session.power()` to request the desired power state.
#[pyclass(skip_from_py_object)]
#[derive(Clone, Copy)]
pub struct Flag(pub u16);

#[pymethods]
#[expect(non_snake_case)]
impl Flag {
    /// AI clock selection.
    ///
    /// Requests maximum AI clock frequency when set; minimum when clear.
    #[classattr]
    fn MaxAiClk() -> Self {
        Self(ffi::TT_POWER_MAX_AI_CLK as u16)
    }

    /// GDDR PHY state.
    ///
    /// Wakes up the GDDR PHY when set; powers it down when clear.
    #[classattr]
    fn MriscPhyWakeup() -> Self {
        Self(ffi::TT_POWER_MRISC_PHY_WAKEUP as u16)
    }

    /// Tensix core gating.
    ///
    /// Enables Tensix cores when set; clock gates them when clear.
    #[classattr]
    fn TensixEnable() -> Self {
        Self(ffi::TT_POWER_TENSIX_ENABLE as u16)
    }

    /// L2CPU clock gating.
    ///
    /// Enables L2CPU when set; clock gates it when clear.
    #[classattr]
    fn L2CpuEnable() -> Self {
        Self(ffi::TT_POWER_L2CPU_ENABLE as u16)
    }

    /// High-power state.
    ///
    /// All power features enabled.
    #[classattr]
    fn HI() -> Self {
        Self(!0)
    }

    /// Low-power state.
    ///
    /// No power features enabled.
    #[classattr]
    fn LO() -> Self {
        Self(0)
    }

    fn __or__(&self, other: &Self) -> Self {
        Self(self.0 | other.0)
    }

    fn __ror__(&self, other: &Self) -> Self {
        Self(self.0 | other.0)
    }

    fn __int__(&self) -> u16 {
        self.0
    }

    fn __repr__(&self) -> String {
        format!("Flag({:#06x})", self.0)
    }
}

use super::Session;

#[pymethods]
impl Session {
    /// Requests a set of power features for this device.
    ///
    /// The `flags` argument describes the complete desired state. Any flag
    /// absent from `flags` is explicitly disabled. The kernel driver aggregates
    /// requests across all open sessions: a feature is enabled if any client
    /// requests it, so the effective state may differ from what any single
    /// client requests. Each client's contribution is removed when its session
    /// is closed.
    ///
    /// Raises `TTError` with:
    ///
    /// - `ENOTCONN` if the session has been closed.
    /// - `ECONNRESET` if the session was severed by an out-of-band device
    ///   reset or removal.
    ///
    /// Other `errno` values propagate from the failing system call.
    pub fn power(&mut self, flags: &Flag) -> PyResult<()> {
        // SAFETY: `sess` is an open device and flags.0 is a valid bitmask.
        self.perform(|sess| crate::err::check(unsafe { ffi::tt_power(sess, flags.0) }))
    }
}
