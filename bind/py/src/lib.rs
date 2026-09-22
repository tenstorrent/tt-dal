// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

//! Tenstorrent Device Access Library.
//!
//! `tt-dal` provides low-level, stateless access to Tenstorrent device
//! hardware through the kernel-mode driver ([KMD][tt-kmd]). It provides raw
//! hardware primitives through an unopinionated device model to support
//! operations such as discovery, memory-mapped I/O via TLB windows, SMC
//! messaging, telemetry, and reset operations. Higher-level libraries should
//! build on this library for application-level device management.
//!
//! [tt-kmd]: https://github.com/tenstorrent/tt-kmd

pub(crate) use ttdal_sys as ffi;
mod dev;
mod err;
mod fw;
mod kmd;
mod ver;

use pyo3::prelude::*;
use pyo3::types::PyModule;

#[pymodule]
fn ttdal(m: &Bound<'_, PyModule>) -> PyResult<()> {
    let py = m.py();

    // Exception
    m.add("TTError", py.get_type::<err::TTError>())?;

    // Device discovery and lifecycle
    m.add_function(wrap_pyfunction!(dev::scan, m)?)?;
    m.add_class::<dev::Device>()?;
    m.add_class::<dev::Session>()?;
    m.add_class::<dev::Info>()?;

    // Library version
    m.add(
        "__version__",
        format!(
            "{}.{}.{}",
            ffi::TTDAL_VERSION_MAJOR,
            ffi::TTDAL_VERSION_MINOR,
            ffi::TTDAL_VERSION_PATCH,
        ),
    )?;

    // kmd submodule
    let kmd = PyModule::new(py, "kmd")?;
    kmd.add_function(wrap_pyfunction!(crate::kmd::version, &kmd)?)?;
    m.add_submodule(&kmd)?;
    py.import("sys")?
        .getattr("modules")?
        .set_item("ttdal.kmd", kmd)?;

    // fw submodule
    let fw = PyModule::new(py, "fw")?;
    fw.add_function(wrap_pyfunction!(crate::fw::version, &fw)?)?;
    m.add_submodule(&fw)?;
    py.import("sys")?
        .getattr("modules")?
        .set_item("ttdal.fw", fw)?;

    // tlb submodule
    let tlb = PyModule::new(py, "tlb")?;
    tlb.add_class::<dev::tlb::Size>()?;
    tlb.add_class::<dev::tlb::Caching>()?;
    tlb.add_class::<dev::tlb::Config>()?;
    tlb.add_class::<dev::tlb::Tlb>()?;
    tlb.add_class::<dev::tlb::Window>()?;
    m.add_submodule(&tlb)?;
    py.import("sys")?
        .getattr("modules")?
        .set_item("ttdal.tlb", tlb)?;

    // power submodule
    let power = PyModule::new(py, "power")?;
    power.add_class::<dev::power::Flag>()?;
    m.add_submodule(&power)?;
    py.import("sys")?
        .getattr("modules")?
        .set_item("ttdal.power", power)?;

    // smc submodule
    let smc = PyModule::new(py, "smc")?;
    smc.add_class::<dev::smc::Message>()?;
    smc.add_class::<dev::smc::Queued>()?;
    m.add_submodule(&smc)?;
    py.import("sys")?
        .getattr("modules")?
        .set_item("ttdal.smc", smc)?;

    // telem submodule
    let telem = PyModule::new(py, "telem")?;
    telem.add_class::<dev::telem::Tag>()?;
    telem.add_class::<dev::telem::Telemetry>()?;
    m.add_submodule(&telem)?;
    py.import("sys")?
        .getattr("modules")?
        .set_item("ttdal.telem", telem)?;

    Ok(())
}
