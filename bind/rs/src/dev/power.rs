//! Power state.
//!
//! Controls which power features are requested to be active on the device. Each
//! [`Flag`] value corresponds to a single feature, enabled when present and
//! disabled when absent.
//!
//! The kernel driver aggregates power requests across all open sessions. A
//! feature stays enabled as long as any session requests it and is withdrawn
//! automatically when that session closes.
//!
//! # Usage
//!
//! Request the power state with [`Session::request_power()`] to enable the
//! desired features for this session.
//!
//! ```no_run
//! # use ttdal::dev::{Device, power::{Flag, FlagSet}};
//! #
//! # let dev = Device::scan().unwrap().next().unwrap();
//! #
//! // Open a device session
//! let con = dev.open()?;
//!
//! // Request a preset high-power state
//! con.request_power(FlagSet::HI)?;
//! // Or request individual flags
//! con.request_power([Flag::MaxAiClk, Flag::TensixEnable])?;
//! #
//! # Ok::<(), ttdal::Error>(())
//! ```

use crate::ffi;
use super::Session;
use crate::{Result, err};

/// Power feature flag.
///
/// A single controllable power feature on the device. Combine flags into a
/// [`FlagSet`] and pass to [`Session::request_power()`] to request the desired
/// power state.
#[repr(u16)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[expect(clippy::cast_possible_truncation)]
pub enum Flag {
    /// AI clock selection.
    ///
    /// Requests maximum AI clock frequency when set; minimum when clear.
    MaxAiClk = ffi::TT_POWER_MAX_AI_CLK as u16,
    /// GDDR PHY state.
    ///
    /// Wakes up the GDDR PHY when set; powers it down when clear.
    MriscPhyWakeup = ffi::TT_POWER_MRISC_PHY_WAKEUP as u16,
    /// Tensix core gating.
    ///
    /// Enables Tensix cores when set; clock gates them when clear.
    TensixEnable = ffi::TT_POWER_TENSIX_ENABLE as u16,
    /// L2CPU clock gating.
    ///
    /// Enables L2CPU when set; clock gates it when clear.
    L2CpuEnable = ffi::TT_POWER_L2CPU_ENABLE as u16,
}

/// A set of [`Flag`] values describing a requested power state.
///
/// Construct the desired power state from individual flags or use one of the
/// default presets:
///
/// - [`FlagSet::HI`]: All power features enabled.
/// - [`FlagSet::LO`]: No power features enabled.
#[repr(transparent)]
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct FlagSet(u16);

impl FlagSet {
    /// High-power state.
    ///
    /// All power features enabled.
    pub const HI: Self = Self(!0);

    /// Low-power state.
    ///
    /// No power features enabled.
    pub const LO: Self = Self(0);
}

impl From<Flag> for FlagSet {
    fn from(flag: Flag) -> Self {
        Self(flag as u16)
    }
}

impl<const N: usize> From<[Flag; N]> for FlagSet {
    fn from(flags: [Flag; N]) -> Self {
        flags.into_iter().collect()
    }
}

impl FromIterator<Flag> for FlagSet {
    fn from_iter<I: IntoIterator<Item = Flag>>(flags: I) -> Self {
        Self(flags.into_iter().fold(0u16, |acc, f| acc | f as u16))
    }
}

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
    /// # Errors
    ///
    /// Returns an error if the kernel driver rejects the request.
    pub fn request_power(&self, flags: impl Into<FlagSet>) -> Result<()> {
        let raw = flags.into().0;
        // SAFETY: `self.0` is an open device and `raw` is a valid bitmask.
        err::check(unsafe { ffi::tt_power(self.as_ptr(), raw) })
    }
}
