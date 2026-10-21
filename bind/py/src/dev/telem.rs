// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

//! Chip telemetry.
//!
//! Provides a snapshot of runtime sensor and status values reported
//! by the device firmware. Values are a flat array of `u32`s indexed
//! by `Tag` variants, each corresponding to a specific measurement
//! such as temperature, clock frequency, or power consumption.

use crate::ffi;
use pyo3::prelude::*;

/// Telemetry tag for indexing a `Telemetry` snapshot.
///
/// Each tag's integer value is its C tag number.
#[pyclass(eq, eq_int, from_py_object)]
#[derive(Clone, Copy, PartialEq)]
pub enum Tag {
    BoardIdHigh = ffi::TT_TAG_BOARD_ID_HIGH as isize,
    BoardIdLow = ffi::TT_TAG_BOARD_ID_LOW as isize,
    AsicId = ffi::TT_TAG_ASIC_ID as isize,
    HarvestingState = ffi::TT_TAG_HARVESTING_STATE as isize,
    UpdateTelemSpeed = ffi::TT_TAG_UPDATE_TELEM_SPEED as isize,
    Vcore = ffi::TT_TAG_VCORE as isize,
    Tdp = ffi::TT_TAG_TDP as isize,
    Tdc = ffi::TT_TAG_TDC as isize,
    VddLimits = ffi::TT_TAG_VDD_LIMITS as isize,
    ThmLimitShutdown = ffi::TT_TAG_THM_LIMIT_SHUTDOWN as isize,
    AsicTemperature = ffi::TT_TAG_ASIC_TEMPERATURE as isize,
    VregTemperature = ffi::TT_TAG_VREG_TEMPERATURE as isize,
    BoardTemperature = ffi::TT_TAG_BOARD_TEMPERATURE as isize,
    AiClk = ffi::TT_TAG_AICLK as isize,
    AxiClk = ffi::TT_TAG_AXICLK as isize,
    SmcClk = ffi::TT_TAG_SMCCLK as isize,
    L2CpuClk0 = ffi::TT_TAG_L2CPUCLK0 as isize,
    L2CpuClk1 = ffi::TT_TAG_L2CPUCLK1 as isize,
    L2CpuClk2 = ffi::TT_TAG_L2CPUCLK2 as isize,
    L2CpuClk3 = ffi::TT_TAG_L2CPUCLK3 as isize,
    EthLiveStatus = ffi::TT_TAG_ETH_LIVE_STATUS as isize,
    GddrStatus = ffi::TT_TAG_GDDR_STATUS as isize,
    GddrSpeed = ffi::TT_TAG_GDDR_SPEED as isize,
    EthFwVersion = ffi::TT_TAG_ETH_FW_VERSION as isize,
    GddrFwVersion = ffi::TT_TAG_GDDR_FW_VERSION as isize,
    DmAppFwVersion = ffi::TT_TAG_DM_APP_FW_VERSION as isize,
    DmBlFwVersion = ffi::TT_TAG_DM_BL_FW_VERSION as isize,
    FlashBundleVersion = ffi::TT_TAG_FLASH_BUNDLE_VERSION as isize,
    CmFwVersion = ffi::TT_TAG_CM_FW_VERSION as isize,
    L2CpuFwVersion = ffi::TT_TAG_L2CPU_FW_VERSION as isize,
    FanSpeed = ffi::TT_TAG_FAN_SPEED as isize,
    TimerHeartbeat = ffi::TT_TAG_TIMER_HEARTBEAT as isize,
    EnabledTensixCol = ffi::TT_TAG_ENABLED_TENSIX_COL as isize,
    EnabledEth = ffi::TT_TAG_ENABLED_ETH as isize,
    EnabledGddr = ffi::TT_TAG_ENABLED_GDDR as isize,
    EnabledL2Cpu = ffi::TT_TAG_ENABLED_L2CPU as isize,
    PcieUsage = ffi::TT_TAG_PCIE_USAGE as isize,
    InputCurrent = ffi::TT_TAG_INPUT_CURRENT as isize,
    NocTranslation = ffi::TT_TAG_NOC_TRANSLATION as isize,
    FanRpm = ffi::TT_TAG_FAN_RPM as isize,
    Gddr01Temp = ffi::TT_TAG_GDDR_0_1_TEMP as isize,
    Gddr23Temp = ffi::TT_TAG_GDDR_2_3_TEMP as isize,
    Gddr45Temp = ffi::TT_TAG_GDDR_4_5_TEMP as isize,
    Gddr67Temp = ffi::TT_TAG_GDDR_6_7_TEMP as isize,
    Gddr01CorrErrs = ffi::TT_TAG_GDDR_0_1_CORR_ERRS as isize,
    Gddr23CorrErrs = ffi::TT_TAG_GDDR_2_3_CORR_ERRS as isize,
    Gddr45CorrErrs = ffi::TT_TAG_GDDR_4_5_CORR_ERRS as isize,
    Gddr67CorrErrs = ffi::TT_TAG_GDDR_6_7_CORR_ERRS as isize,
    GddrUncorrErrs = ffi::TT_TAG_GDDR_UNCORR_ERRS as isize,
    MaxGddrTemp = ffi::TT_TAG_MAX_GDDR_TEMP as isize,
    AsicLocation = ffi::TT_TAG_ASIC_LOCATION as isize,
    BoardPowerLimit = ffi::TT_TAG_BOARD_POWER_LIMIT as isize,
    InputPower = ffi::TT_TAG_INPUT_POWER as isize,
    TdcLimitMax = ffi::TT_TAG_TDC_LIMIT_MAX as isize,
    ThmLimitThrottle = ffi::TT_TAG_THM_LIMIT_THROTTLE as isize,
    FwBuildDate = ffi::TT_TAG_FW_BUILD_DATE as isize,
    TtFlashVersion = ffi::TT_TAG_TT_FLASH_VERSION as isize,
    EnabledTensixRow = ffi::TT_TAG_ENABLED_TENSIX_ROW as isize,
    ThermTripCount = ffi::TT_TAG_THERM_TRIP_COUNT as isize,
    AsicIdHigh = ffi::TT_TAG_ASIC_ID_HIGH as isize,
    AsicIdLow = ffi::TT_TAG_ASIC_ID_LOW as isize,
    AiClkLimitMax = ffi::TT_TAG_AICLK_LIMIT_MAX as isize,
    TdpLimitMax = ffi::TT_TAG_TDP_LIMIT_MAX as isize,
    AiClkArbMin = ffi::TT_TAG_AICLK_ARB_MIN as isize,
    AiClkArbMax = ffi::TT_TAG_AICLK_ARB_MAX as isize,
    EnabledMinArb = ffi::TT_TAG_ENABLED_MIN_ARB as isize,
    EnabledMaxArb = ffi::TT_TAG_ENABLED_MAX_ARB as isize,
}

impl Tag {
    /// Returns the raw telemetry tag index.
    fn as_raw(&self) -> u32 {
        *self as u32
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
    /// - `ECONNRESET` if the session was severed by an out-of-band device
    ///   reset or removal.
    /// - `EIO` if the telemetry data is malformed or cannot be read.
    /// - `ENOTSUP` if the device architecture is unsupported.
    ///
    /// Other `errno` values propagate from the failing system call.
    pub fn telemetry(&mut self) -> PyResult<Telemetry> {
        self.perform(|sess| {
            let mut table = vec![0u32; ffi::TT_TELEMETRY_LEN as usize];
            // SAFETY: `sess` is an open device and table.as_mut_ptr() is valid
            // for TT_TELEMETRY_LEN u32 writes.
            crate::err::check(unsafe { ffi::tt_telemetry(sess, table.as_mut_ptr()) })?;
            Ok(Telemetry::from_raw(table))
        })
    }
}
