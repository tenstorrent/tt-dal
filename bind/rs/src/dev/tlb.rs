//! Memory windows.
//!
//! Translation Lookaside Buffer (TLB) windows map a region of device NOC
//! address space into host-accessible memory. Access follows a three-step
//! lifetime chain:
//!
//! 1. [Allocate](Session::alloc) a [`Tlb`], specifying window size and caching.
//! 2. [Bind](Tlb::bind) the [`Tlb`] to a NOC address, obtaining a [`Window`].
//! 3. [Read](Window::read) or [write](Window::write) device memory through the
//!    [`Window`].
//!
//! Dropping the [`Window`] releases the mapping and allows [`Tlb::bind()`] to
//! be called again. Dropping the [`Tlb`] frees the allocation.
//!
//! # Usage
//!
//! Allocate a [`Tlb`] with [`Session::alloc()`], bind it to a NOC address via
//! [`Tlb::bind()`], then read or write through the resulting [`Window`].
//!
//! ```no_run
//! # use ttdal::dev::{Device, tlb::{Caching, Config, Size}};
//! #
//! # let dev = Device::scan().unwrap().next().unwrap();
//! #
//! // Open a device session
//! let con = dev.open()?;
//!
//! // Allocate and bind a TLB window
//! let mut tlb = con.alloc(Size::Mb2, Caching::WriteCombined)?;
//! # let cfg: Config = unsafe { std::mem::zeroed() };
//! let win = tlb.bind(&cfg)?;
//!
//! // Read a value from device memory
//! let val: u32 = unsafe { win.read(0) };
//! println!("read: {val:#010x}");
//! #
//! # Ok::<(), ttdal::Error>(())
//! ```

use crate::ffi;
use super::Session;
use crate::{Result, err};

/// TLB window size.
///
/// Not all sizes are available on all architectures. The kernel rejects
/// unsupported sizes at allocation time.
#[repr(u64)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Size {
    /// 1 MB window.
    ///
    /// Supported: Wormhole.
    Mb1 = ffi::TT_TLB_1MB,
    /// 2 MB window.
    ///
    /// Supported: Wormhole, Blackhole.
    Mb2 = ffi::TT_TLB_2MB,
    /// 16 MB window.
    ///
    /// Supported: Wormhole.
    Mb16 = ffi::TT_TLB_16MB,
    /// 4 GB window.
    ///
    /// Supported: Blackhole.
    Gb4 = ffi::TT_TLB_4GB,
}

/// TLB cache mode.
#[repr(u32)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Caching {
    /// Uncached.
    ///
    /// Use for register access where ordering and immediate visibility matter.
    Uncached = ffi::TT_TLB_UC,
    /// Write-combined.
    ///
    /// Use for memory access where batching writes improves performance.
    WriteCombined = ffi::TT_TLB_WC,
}

/// TLB window configuration.
pub type Config = ffi::tt_tlb_config_t;

/// An allocated TLB window bound to a [`Session`].
///
/// The TLB is freed automatically when dropped. The window is not mapped until
/// [`Tlb::bind()`] is called.
#[derive(Debug)]
pub struct Tlb<'dev> {
    raw: ffi::tt_tlb_t,
    dev: &'dev Session,
}

impl<'dev> Tlb<'dev> {
    /// Maps the TLB to the given NOC address and coordinates, returning a
    /// [`Window`] for memory access.
    ///
    /// Safe to call again after the previous [`Window`] has been dropped.
    ///
    /// # Errors
    ///
    /// Returns an error if the kernel driver rejects the configuration.
    #[doc(alias = "configure")]
    pub fn bind<'tlb>(&'tlb mut self, cfg: &Config) -> Result<Window<'tlb, 'dev>> {
        // SAFETY: `self.dev.0` is an open device, `self.raw` is a TLB allocated
        // by `tt_tlb_alloc`, and `cfg` is a valid configuration.
        err::check(unsafe { ffi::tt_tlb_bind(self.dev.as_ptr(), &raw mut self.raw, cfg) })?;
        Ok(Window { tlb: self })
    }
}

impl Drop for Tlb<'_> {
    fn drop(&mut self) {
        // SAFETY: `self.dev` is an open device and `self.raw` is a TLB allocated
        // by `tt_tlb_alloc`. Freeing here is safe because `Drop` runs exactly
        // once when the `Tlb` goes out of scope.
        let _ = unsafe { ffi::tt_tlb_free(self.dev.as_ptr(), &raw mut self.raw) };
    }
}

/// A mapped TLB window bound to a [`Tlb`].
///
/// Provides volatile read/write access to device memory. Dropping the window
/// releases the exclusive borrow on [`Tlb`], allowing [`Tlb::bind()`] to be
/// called again.
pub struct Window<'tlb, 'dev> {
    tlb: &'tlb mut Tlb<'dev>,
}

impl Window<'_, '_> {
    /// Returns the window size in bytes.
    #[must_use]
    pub fn size(&self) -> usize {
        self.tlb.raw.len
    }

    /// Reads a `T`-sized value at `offset` bytes from the window base using a
    /// volatile read.
    ///
    /// # Safety
    ///
    /// `offset` must be in-bounds and correctly aligned for `T`. The hardware
    /// at that address must support a read of this width.
    #[must_use]
    pub unsafe fn read<T: Copy>(&self, offset: usize) -> T {
        // SAFETY: caller guarantees `offset` is in-bounds and aligned for `T`.
        unsafe {
            let ptr = (self.tlb.raw.ptr as *const u8).add(offset).cast::<T>();
            ptr.read_volatile()
        }
    }

    /// Writes a `T`-sized value at `offset` bytes from the window base using a
    /// volatile write.
    ///
    /// # Safety
    ///
    /// `offset` must be in-bounds and correctly aligned for `T`. The hardware
    /// at that address must support a write of this width.
    pub unsafe fn write<T: Copy>(&self, offset: usize, val: T) {
        // SAFETY: caller guarantees `offset` is in-bounds and aligned for `T`.
        unsafe {
            let ptr = self.tlb.raw.ptr.cast::<u8>().add(offset).cast::<T>();
            ptr.write_volatile(val);
        }
    }
}

impl Session {
    /// Allocates a TLB window of the given size and cache mode.
    ///
    /// The window is not mapped until [`Tlb::bind()`] is called.
    ///
    /// # Errors
    ///
    /// Returns an error if the kernel driver fails to allocate the window.
    pub fn alloc(&self, size: Size, mode: Caching) -> Result<Tlb<'_>> {
        let mut raw = std::mem::MaybeUninit::<ffi::tt_tlb_t>::uninit();
        // SAFETY: `self.0` is an open device and `raw` is a valid out-pointer
        // for `tt_tlb_t`.
        err::check(unsafe {
            ffi::tt_tlb_alloc(self.as_ptr(), size as u64, mode as u32, raw.as_mut_ptr())
        })?;
        // SAFETY: `raw` was fully initialized by the successful call above.
        Ok(Tlb {
            raw: unsafe { raw.assume_init() },
            dev: self,
        })
    }
}

#[cfg(test)]
mod tests {
    use serial_test::serial;

    use super::*;

    #[test]
    fn size_nonzero() {
        assert!(Size::Mb1 as u64 > 0);
        assert!(Size::Mb2 as u64 > 0);
        assert!(Size::Mb16 as u64 > 0);
        assert!(Size::Gb4 as u64 > 0);
    }

    #[test]
    fn caching_distinct() {
        assert_ne!(Caching::Uncached as u32, Caching::WriteCombined as u32);
    }

    #[test]
    #[ignore]
    #[serial]
    fn alloc_smoke() {
        let dev = crate::tests::open();
        let _tlb = dev
            .alloc(Size::Mb2, Caching::WriteCombined)
            .expect("alloc failed");
    }

    #[test]
    #[ignore]
    #[serial]
    fn bind_smoke() {
        let dev = crate::tests::open();
        let mut tlb = dev
            .alloc(Size::Mb2, Caching::WriteCombined)
            .expect("alloc failed");
        let cfg = Config {
            addr: 0,
            x_end: 0,
            y_end: 0,
            x_start: 0,
            y_start: 0,
            noc: 0,
            mcast: false,
            linked: false,
            static_vc: 0,
        };
        let win = tlb.bind(&cfg).expect("bind failed");
        assert!(win.size() > 0);
    }

    #[test]
    #[ignore]
    #[serial]
    fn read_write() {
        let dev = crate::tests::open();
        let mut tlb = dev
            .alloc(Size::Mb2, Caching::Uncached)
            .expect("alloc failed");
        let cfg = Config {
            addr: 0,
            x_end: 0,
            y_end: 0,
            x_start: 0,
            y_start: 0,
            noc: 0,
            mcast: false,
            linked: false,
            static_vc: 0,
        };
        let win = tlb.bind(&cfg).expect("bind failed");
        // Read a u32, write the same value back, then read again: a no-op
        // round-trip that exercises both volatile paths without corrupting
        // state.
        let val: u32 = unsafe { win.read(0) };
        unsafe { win.write(0, val) };
        let readback: u32 = unsafe { win.read(0) };
        assert_eq!(val, readback);
    }
}
