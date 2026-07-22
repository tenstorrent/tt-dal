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

#![warn(clippy::pedantic)]

pub(crate) use ttdal_sys as ffi;
pub mod dev;
pub mod ver;

mod err;

/// Device architecture.
///
/// Architecture specifier for a Tenstorrent device architecture generation.
/// Variants are assigned their corresponding PCI device IDs.
#[repr(u32)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[expect(clippy::doc_markdown)]
pub enum Arch {
    /// Grayskull.
    ///
    /// PCIe device ID: `0xffa0`.
    ///
    /// # Note
    ///
    /// This is a legacy architecture that is no longer supported.
    #[deprecated]
    Grayskull = ffi::TT_ARCH_GRAYSKULL,
    /// Wormhole.
    ///
    /// PCIe device ID: `0x401e`.
    Wormhole = ffi::TT_ARCH_WORMHOLE,
    /// Blackhole.
    ///
    /// PCIe device ID: `0xb140`.
    Blackhole = ffi::TT_ARCH_BLACKHOLE,
}

#[doc(inline)]
pub use self::dev::Device;
#[doc(inline)]
pub use self::err::{Error, Result};

#[cfg(test)]
pub(crate) mod tests {
    pub fn open() -> crate::dev::Session {
        let dev = crate::dev::Device::scan()
            .expect("device scan failed")
            .next()
            .expect("no device found");
        crate::dev::Session::open(dev).expect("device open failed")
    }
}
