//! Chip telemetry.
//!
//! Provides a snapshot of runtime sensor and status values reported
//! by the device firmware. Values are a flat array of `u32`s indexed
//! by [`Tag`] variants, each corresponding to a specific measurement
//! such as temperature, clock frequency, or power consumption.
//!
//! # Usage
//!
//! Read a telemetry snapshot with [`Session::telemetry()`] and index
//! into it using a [`Tag`] variant.
//!
//! ```no_run
//! # use ttdal::dev::{Device, Session, telem::Tag};
//! #
//! # let dev = Device::scan().unwrap().next().unwrap();
//! #
//! let sess = Session::open(dev)?;
//! let telem = sess.telemetry()?;
//!
//! let temp = telem[Tag::AsicTemperature];
//! let aiclk = telem.get(Tag::AiClk);
//! #
//! # Ok::<(), ttdal::Error>(())
//! ```

use crate::ffi;
use std::ops::Index;

use super::Session;
use crate::{Result, err};

/// Telemetry tag.
#[repr(u32)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Tag {
    BoardIdHigh = ffi::TT_TAG_BOARD_ID_HIGH,
    BoardIdLow = ffi::TT_TAG_BOARD_ID_LOW,
    AsicId = ffi::TT_TAG_ASIC_ID,
    HarvestingState = ffi::TT_TAG_HARVESTING_STATE,
    UpdateTelemSpeed = ffi::TT_TAG_UPDATE_TELEM_SPEED,
    Vcore = ffi::TT_TAG_VCORE,
    Tdp = ffi::TT_TAG_TDP,
    Tdc = ffi::TT_TAG_TDC,
    VddLimits = ffi::TT_TAG_VDD_LIMITS,
    ThmLimitShutdown = ffi::TT_TAG_THM_LIMIT_SHUTDOWN,
    AsicTemperature = ffi::TT_TAG_ASIC_TEMPERATURE,
    VregTemperature = ffi::TT_TAG_VREG_TEMPERATURE,
    BoardTemperature = ffi::TT_TAG_BOARD_TEMPERATURE,
    AiClk = ffi::TT_TAG_AICLK,
    AxiClk = ffi::TT_TAG_AXICLK,
    ArcClk = ffi::TT_TAG_ARCCLK,
    L2CpuClk0 = ffi::TT_TAG_L2CPUCLK0,
    L2CpuClk1 = ffi::TT_TAG_L2CPUCLK1,
    L2CpuClk2 = ffi::TT_TAG_L2CPUCLK2,
    L2CpuClk3 = ffi::TT_TAG_L2CPUCLK3,
    EthLiveStatus = ffi::TT_TAG_ETH_LIVE_STATUS,
    GddrStatus = ffi::TT_TAG_GDDR_STATUS,
    GddrSpeed = ffi::TT_TAG_GDDR_SPEED,
    EthFwVersion = ffi::TT_TAG_ETH_FW_VERSION,
    GddrFwVersion = ffi::TT_TAG_GDDR_FW_VERSION,
    DmAppFwVersion = ffi::TT_TAG_DM_APP_FW_VERSION,
    DmBlFwVersion = ffi::TT_TAG_DM_BL_FW_VERSION,
    FlashBundleVersion = ffi::TT_TAG_FLASH_BUNDLE_VERSION,
    CmFwVersion = ffi::TT_TAG_CM_FW_VERSION,
    L2CpuFwVersion = ffi::TT_TAG_L2CPU_FW_VERSION,
    FanSpeed = ffi::TT_TAG_FAN_SPEED,
    TimerHeartbeat = ffi::TT_TAG_TIMER_HEARTBEAT,
    EnabledTensixCol = ffi::TT_TAG_ENABLED_TENSIX_COL,
    EnabledEth = ffi::TT_TAG_ENABLED_ETH,
    EnabledGddr = ffi::TT_TAG_ENABLED_GDDR,
    EnabledL2Cpu = ffi::TT_TAG_ENABLED_L2CPU,
    PcieUsage = ffi::TT_TAG_PCIE_USAGE,
    InputCurrent = ffi::TT_TAG_INPUT_CURRENT,
    NocTranslation = ffi::TT_TAG_NOC_TRANSLATION,
    FanRpm = ffi::TT_TAG_FAN_RPM,
    Gddr01Temp = ffi::TT_TAG_GDDR_0_1_TEMP,
    Gddr23Temp = ffi::TT_TAG_GDDR_2_3_TEMP,
    Gddr45Temp = ffi::TT_TAG_GDDR_4_5_TEMP,
    Gddr67Temp = ffi::TT_TAG_GDDR_6_7_TEMP,
    Gddr01CorrErrs = ffi::TT_TAG_GDDR_0_1_CORR_ERRS,
    Gddr23CorrErrs = ffi::TT_TAG_GDDR_2_3_CORR_ERRS,
    Gddr45CorrErrs = ffi::TT_TAG_GDDR_4_5_CORR_ERRS,
    Gddr67CorrErrs = ffi::TT_TAG_GDDR_6_7_CORR_ERRS,
    GddrUncorrErrs = ffi::TT_TAG_GDDR_UNCORR_ERRS,
    MaxGddrTemp = ffi::TT_TAG_MAX_GDDR_TEMP,
    AsicLocation = ffi::TT_TAG_ASIC_LOCATION,
    BoardPowerLimit = ffi::TT_TAG_BOARD_POWER_LIMIT,
    InputPower = ffi::TT_TAG_INPUT_POWER,
    TdcLimitMax = ffi::TT_TAG_TDC_LIMIT_MAX,
    ThmLimitThrottle = ffi::TT_TAG_THM_LIMIT_THROTTLE,
    FwBuildDate = ffi::TT_TAG_FW_BUILD_DATE,
    TtFlashVersion = ffi::TT_TAG_TT_FLASH_VERSION,
    EnabledTensixRow = ffi::TT_TAG_ENABLED_TENSIX_ROW,
    ThermTripCount = ffi::TT_TAG_THERM_TRIP_COUNT,
    AsicIdHigh = ffi::TT_TAG_ASIC_ID_HIGH,
    AsicIdLow = ffi::TT_TAG_ASIC_ID_LOW,
    AiClkLimitMax = ffi::TT_TAG_AICLK_LIMIT_MAX,
    TdpLimitMax = ffi::TT_TAG_TDP_LIMIT_MAX,
    AiClkArbMin = ffi::TT_TAG_AICLK_ARB_MIN,
    AiClkArbMax = ffi::TT_TAG_AICLK_ARB_MAX,
    EnabledMinArb = ffi::TT_TAG_ENABLED_MIN_ARB,
    EnabledMaxArb = ffi::TT_TAG_ENABLED_MAX_ARB,
}

/// A snapshot of device telemetry values, indexed by [`Tag`].
#[derive(Clone, Debug, PartialEq)]
pub struct Telemetry([u32; ffi::TT_TELEMETRY_LEN as usize]);

impl Telemetry {
    /// Returns the value for `tag`, or `None` if the tag is out of range.
    #[must_use]
    pub fn get(&self, tag: Tag) -> Option<u32> {
        self.0.get(tag as usize).copied()
    }
}

impl Index<Tag> for Telemetry {
    type Output = u32;

    fn index(&self, tag: Tag) -> &u32 {
        &self.0[tag as usize]
    }
}

impl Session {
    /// Reads a complete telemetry snapshot.
    ///
    /// Tags the firmware does not report read as zero.
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
        self.call(|sess| {
            let mut table = [0u32; ffi::TT_TELEMETRY_LEN as usize];
            // SAFETY: `sess` is an open device and `table` is valid for
            // `TT_TELEMETRY_LEN` u32 writes.
            err::check(unsafe { ffi::tt_telemetry(sess.as_ptr(), table.as_mut_ptr()) })?;
            Ok(Telemetry(table))
        })
    }
}
