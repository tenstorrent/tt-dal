//! Chip telemetry.
//!
//! Provides a snapshot of runtime sensor and status values reported
//! by the device firmware. Values are a flat array of `u32`s indexed
//! by `Tag` variants, each corresponding to a specific measurement
//! such as temperature, clock frequency, or power consumption.

use crate::ffi;
use pyo3::prelude::*;

/// Telemetry tag for indexing a `Telemetry` snapshot.
#[pyclass(eq, eq_int, from_py_object)]
#[derive(Clone, Copy, PartialEq)]
pub enum Tag {
    BoardIdHigh,
    BoardIdLow,
    AsicId,
    HarvestingState,
    UpdateTelemSpeed,
    Vcore,
    Tdp,
    Tdc,
    VddLimits,
    ThmLimitShutdown,
    AsicTemperature,
    VregTemperature,
    BoardTemperature,
    AiClk,
    AxiClk,
    ArcClk,
    L2CpuClk0,
    L2CpuClk1,
    L2CpuClk2,
    L2CpuClk3,
    EthLiveStatus,
    GddrStatus,
    GddrSpeed,
    EthFwVersion,
    GddrFwVersion,
    DmAppFwVersion,
    DmBlFwVersion,
    FlashBundleVersion,
    CmFwVersion,
    L2CpuFwVersion,
    FanSpeed,
    TimerHeartbeat,
    EnabledTensixCol,
    EnabledEth,
    EnabledGddr,
    EnabledL2Cpu,
    PcieUsage,
    InputCurrent,
    NocTranslation,
    FanRpm,
    Gddr01Temp,
    Gddr23Temp,
    Gddr45Temp,
    Gddr67Temp,
    Gddr01CorrErrs,
    Gddr23CorrErrs,
    Gddr45CorrErrs,
    Gddr67CorrErrs,
    GddrUncorrErrs,
    MaxGddrTemp,
    AsicLocation,
    BoardPowerLimit,
    InputPower,
    TdcLimitMax,
    ThmLimitThrottle,
    FwBuildDate,
    TtFlashVersion,
    EnabledTensixRow,
    ThermTripCount,
    AsicIdHigh,
    AsicIdLow,
    AiClkLimitMax,
    TdpLimitMax,
    AiClkArbMin,
    AiClkArbMax,
    EnabledMinArb,
    EnabledMaxArb,
}

impl Tag {
    /// Returns the raw telemetry tag index.
    fn as_raw(&self) -> u32 {
        match self {
            Self::BoardIdHigh => ffi::TT_TAG_BOARD_ID_HIGH,
            Self::BoardIdLow => ffi::TT_TAG_BOARD_ID_LOW,
            Self::AsicId => ffi::TT_TAG_ASIC_ID,
            Self::HarvestingState => ffi::TT_TAG_HARVESTING_STATE,
            Self::UpdateTelemSpeed => ffi::TT_TAG_UPDATE_TELEM_SPEED,
            Self::Vcore => ffi::TT_TAG_VCORE,
            Self::Tdp => ffi::TT_TAG_TDP,
            Self::Tdc => ffi::TT_TAG_TDC,
            Self::VddLimits => ffi::TT_TAG_VDD_LIMITS,
            Self::ThmLimitShutdown => ffi::TT_TAG_THM_LIMIT_SHUTDOWN,
            Self::AsicTemperature => ffi::TT_TAG_ASIC_TEMPERATURE,
            Self::VregTemperature => ffi::TT_TAG_VREG_TEMPERATURE,
            Self::BoardTemperature => ffi::TT_TAG_BOARD_TEMPERATURE,
            Self::AiClk => ffi::TT_TAG_AICLK,
            Self::AxiClk => ffi::TT_TAG_AXICLK,
            Self::ArcClk => ffi::TT_TAG_ARCCLK,
            Self::L2CpuClk0 => ffi::TT_TAG_L2CPUCLK0,
            Self::L2CpuClk1 => ffi::TT_TAG_L2CPUCLK1,
            Self::L2CpuClk2 => ffi::TT_TAG_L2CPUCLK2,
            Self::L2CpuClk3 => ffi::TT_TAG_L2CPUCLK3,
            Self::EthLiveStatus => ffi::TT_TAG_ETH_LIVE_STATUS,
            Self::GddrStatus => ffi::TT_TAG_GDDR_STATUS,
            Self::GddrSpeed => ffi::TT_TAG_GDDR_SPEED,
            Self::EthFwVersion => ffi::TT_TAG_ETH_FW_VERSION,
            Self::GddrFwVersion => ffi::TT_TAG_GDDR_FW_VERSION,
            Self::DmAppFwVersion => ffi::TT_TAG_DM_APP_FW_VERSION,
            Self::DmBlFwVersion => ffi::TT_TAG_DM_BL_FW_VERSION,
            Self::FlashBundleVersion => ffi::TT_TAG_FLASH_BUNDLE_VERSION,
            Self::CmFwVersion => ffi::TT_TAG_CM_FW_VERSION,
            Self::L2CpuFwVersion => ffi::TT_TAG_L2CPU_FW_VERSION,
            Self::FanSpeed => ffi::TT_TAG_FAN_SPEED,
            Self::TimerHeartbeat => ffi::TT_TAG_TIMER_HEARTBEAT,
            Self::EnabledTensixCol => ffi::TT_TAG_ENABLED_TENSIX_COL,
            Self::EnabledEth => ffi::TT_TAG_ENABLED_ETH,
            Self::EnabledGddr => ffi::TT_TAG_ENABLED_GDDR,
            Self::EnabledL2Cpu => ffi::TT_TAG_ENABLED_L2CPU,
            Self::PcieUsage => ffi::TT_TAG_PCIE_USAGE,
            Self::InputCurrent => ffi::TT_TAG_INPUT_CURRENT,
            Self::NocTranslation => ffi::TT_TAG_NOC_TRANSLATION,
            Self::FanRpm => ffi::TT_TAG_FAN_RPM,
            Self::Gddr01Temp => ffi::TT_TAG_GDDR_0_1_TEMP,
            Self::Gddr23Temp => ffi::TT_TAG_GDDR_2_3_TEMP,
            Self::Gddr45Temp => ffi::TT_TAG_GDDR_4_5_TEMP,
            Self::Gddr67Temp => ffi::TT_TAG_GDDR_6_7_TEMP,
            Self::Gddr01CorrErrs => ffi::TT_TAG_GDDR_0_1_CORR_ERRS,
            Self::Gddr23CorrErrs => ffi::TT_TAG_GDDR_2_3_CORR_ERRS,
            Self::Gddr45CorrErrs => ffi::TT_TAG_GDDR_4_5_CORR_ERRS,
            Self::Gddr67CorrErrs => ffi::TT_TAG_GDDR_6_7_CORR_ERRS,
            Self::GddrUncorrErrs => ffi::TT_TAG_GDDR_UNCORR_ERRS,
            Self::MaxGddrTemp => ffi::TT_TAG_MAX_GDDR_TEMP,
            Self::AsicLocation => ffi::TT_TAG_ASIC_LOCATION,
            Self::BoardPowerLimit => ffi::TT_TAG_BOARD_POWER_LIMIT,
            Self::InputPower => ffi::TT_TAG_INPUT_POWER,
            Self::TdcLimitMax => ffi::TT_TAG_TDC_LIMIT_MAX,
            Self::ThmLimitThrottle => ffi::TT_TAG_THM_LIMIT_THROTTLE,
            Self::FwBuildDate => ffi::TT_TAG_FW_BUILD_DATE,
            Self::TtFlashVersion => ffi::TT_TAG_TT_FLASH_VERSION,
            Self::EnabledTensixRow => ffi::TT_TAG_ENABLED_TENSIX_ROW,
            Self::ThermTripCount => ffi::TT_TAG_THERM_TRIP_COUNT,
            Self::AsicIdHigh => ffi::TT_TAG_ASIC_ID_HIGH,
            Self::AsicIdLow => ffi::TT_TAG_ASIC_ID_LOW,
            Self::AiClkLimitMax => ffi::TT_TAG_AICLK_LIMIT_MAX,
            Self::TdpLimitMax => ffi::TT_TAG_TDP_LIMIT_MAX,
            Self::AiClkArbMin => ffi::TT_TAG_AICLK_ARB_MIN,
            Self::AiClkArbMax => ffi::TT_TAG_AICLK_ARB_MAX,
            Self::EnabledMinArb => ffi::TT_TAG_ENABLED_MIN_ARB,
            Self::EnabledMaxArb => ffi::TT_TAG_ENABLED_MAX_ARB,
        }
    }
}

/// A snapshot of device telemetry values, indexed by `Tag`.
#[pyclass]
pub struct Telemetry {
    data: Vec<u32>,
}

#[pymethods]
impl Telemetry {
    /// Returns the value for `tag`, or `None` if the tag is out of range.
    fn get(&self, tag: Tag) -> Option<u32> {
        self.data.get(tag.as_raw() as usize).copied()
    }

    /// Returns the value for `tag`.
    ///
    /// Raises `IndexError` if the tag is out of range.
    fn __getitem__(&self, tag: Tag) -> PyResult<u32> {
        self.get(tag)
            .ok_or_else(|| pyo3::exceptions::PyIndexError::new_err("telemetry tag out of range"))
    }

    fn __repr__(&self) -> String {
        format!("Telemetry([{} entries])", self.data.len())
    }
}

impl Telemetry {
    /// Wraps a raw telemetry table.
    pub(crate) fn from_raw(data: Vec<u32>) -> Self {
        Self { data }
    }
}

use super::Session;

#[pymethods]
impl Session {
    /// Reads a complete telemetry snapshot.
    ///
    /// Tags the firmware does not report read as zero.
    ///
    /// Raises `TTError` with:
    ///
    /// - `ENOTCONN` if the session has been closed.
    /// - `EIO` if the telemetry data is malformed or cannot be read.
    /// - `ENOTSUP` if the device architecture is unsupported.
    ///
    /// Other `errno` values propagate from the failing system call.
    pub fn telemetry(&self) -> PyResult<Telemetry> {
        let mut table = vec![0u32; ffi::TT_TELEMETRY_LEN as usize];
        // SAFETY: Self is an open device and table.as_mut_ptr() is valid for
        // TT_TELEMETRY_LEN u32 writes.
        crate::err::check(unsafe { ffi::tt_telemetry(Session::as_ptr(self), table.as_mut_ptr()) })?;
        Ok(Telemetry::from_raw(table))
    }
}
