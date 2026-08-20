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
//! Request the power state with [`Session::power()`] to enable the
//! desired features for this session.
//!
//! ```no_run
//! # use ttdal::dev::{Device, Session, power::{Flag, FlagSet}};
//! #
//! # let dev = Device::scan().unwrap().next().unwrap();
//! #
//! // Open a device session
//! let con = Session::open(dev)?;
//!
//! // Request a preset high-power state
//! con.power(FlagSet::HI)?;
//! // Or request individual flags
//! con.power([Flag::MaxAiClk, Flag::TensixEnable])?;
//! #
//! # Ok::<(), ttdal::Error>(())
//! ```

use super::Session;
use crate::ffi;
use crate::{Result, err};

/// Power feature flag.
///
/// A single controllable power feature on the device. Combine flags into a
/// [`FlagSet`] and pass to [`Session::power()`] to request the desired
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
    /// Returns [`ConnectionReset`] if the session was severed by an
    /// out-of-band device reset or removal. Other `errno` values from the
    /// failing `ioctl` propagate unchanged, observable via
    /// [`Error::raw_os_error()`].
    ///
    /// [`ConnectionReset`]: std::io::ErrorKind::ConnectionReset
    /// [`Error::raw_os_error()`]: crate::Error::raw_os_error
    pub fn power(&self, flags: impl Into<FlagSet>) -> Result<()> {
        let raw = flags.into().0;
        // SAFETY: `sess` is an open device and `raw` is a valid bitmask.
        self.perform(|sess| err::check(unsafe { ffi::tt_power(sess.as_ptr(), raw) }))
    }
}

#[cfg(test)]
mod tests {
    use serial_test::serial;

    use super::*;

    #[test]
    fn flagset_lo() {
        assert_eq!(FlagSet::LO.0, 0);
    }

    #[test]
    fn flagset_hi() {
        assert_eq!(FlagSet::HI.0, !0u16);
    }

    #[test]
    fn flagset_default() {
        assert_eq!(FlagSet::default(), FlagSet::LO);
    }

    #[test]
    fn flagset_from_flag() {
        let fs = FlagSet::from(Flag::MaxAiClk);
        assert_eq!(fs.0, Flag::MaxAiClk as u16);
    }

    #[test]
    fn flagset_from_array() {
        let fs = FlagSet::from([Flag::MaxAiClk, Flag::TensixEnable]);
        assert_eq!(fs.0, Flag::MaxAiClk as u16 | Flag::TensixEnable as u16);
    }

    #[test]
    fn flagset_from_iter() {
        let flags = [Flag::MriscPhyWakeup, Flag::L2CpuEnable];
        let fs: FlagSet = flags.into_iter().collect();
        assert_eq!(fs.0, Flag::MriscPhyWakeup as u16 | Flag::L2CpuEnable as u16);
    }

    #[test]
    #[ignore]
    #[serial]
    fn power_hi() {
        crate::tests::open()
            .power(FlagSet::HI)
            .expect("power HI failed");
    }

    #[test]
    #[ignore]
    #[serial]
    fn power_lo() {
        crate::tests::open()
            .power(FlagSet::LO)
            .expect("power LO failed");
    }
}
