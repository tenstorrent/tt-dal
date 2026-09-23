// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

//! PCI address types.
//!
//! This module provides [`Address`], a PCI BDF (Bus/Device/Function) address
//! type modeled on [`std::net::Ipv4Addr`].

use std::fmt;
use std::str::FromStr;

/// A PCI address.
///
/// PCI addresses consist of a 16-bit domain (segment), 8-bit bus, 5-bit device,
/// and 3-bit function number, as defined by the PCI Express Base Specification.
///
/// # Textual representation
///
/// `Address` implements [`Display`] and [`FromStr`]. Two formats are accepted:
///
/// * `DDDD:BB:DD.F` - domain, bus, device, function (all hex)
/// * `BB:DD.F` - bus, device, function (domain assumed `0000`)
///
/// # Examples
///
/// ```
/// use ttdal::pci::Address;
///
/// let addr = Address::new(0, 3, 0, 0);
/// assert_eq!(addr.to_string(), "0000:03:00.0");
///
/// let parsed: Address = "03:00.0".parse().unwrap();
/// assert_eq!(parsed, addr);
/// ```
///
/// [`Display`]: fmt::Display
#[derive(Clone, Copy, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct Address {
    /// Domain (segment) number.
    domain: u16,
    /// Bus number.
    bus: u8,
    /// Device number.
    device: u8,
    /// Function number.
    function: u8,
}

impl Address {
    /// Creates a new PCI address from its components.
    #[must_use]
    pub const fn new(domain: u16, bus: u8, device: u8, function: u8) -> Self {
        Self {
            domain,
            bus,
            device,
            function,
        }
    }

    /// Returns the domain (segment) number.
    #[must_use]
    pub const fn domain(&self) -> u16 {
        self.domain
    }

    /// Returns the bus number.
    #[must_use]
    pub const fn bus(&self) -> u8 {
        self.bus
    }

    /// Returns the device number.
    #[must_use]
    pub const fn device(&self) -> u8 {
        self.device
    }

    /// Returns the function number.
    #[must_use]
    pub const fn function(&self) -> u8 {
        self.function
    }
}

impl fmt::Display for Address {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{:04x}:{:02x}:{:02x}.{:x}",
            self.domain, self.bus, self.device, self.function
        )
    }
}

/// The error type returned when parsing a [`Address`] fails.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct AddrParseError(());

impl fmt::Display for AddrParseError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str("invalid PCI address")
    }
}

impl std::error::Error for AddrParseError {}

impl FromStr for Address {
    type Err = AddrParseError;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        let err = AddrParseError(());
        let colon_count = s.chars().filter(|&c| c == ':').count();
        match colon_count {
            2 => {
                // "DDDD:BB:DD.F"
                let (domain_s, rest) = s.split_once(':').ok_or(err)?;
                let (bus_s, devfn_s) = rest.split_once(':').ok_or(err)?;
                let (dev_s, fn_s) = devfn_s.split_once('.').ok_or(err)?;
                Ok(Address::new(
                    u16::from_str_radix(domain_s, 16).map_err(|_| err)?,
                    u8::from_str_radix(bus_s, 16).map_err(|_| err)?,
                    u8::from_str_radix(dev_s, 16).map_err(|_| err)?,
                    u8::from_str_radix(fn_s, 16).map_err(|_| err)?,
                ))
            }
            1 => {
                // "BB:DD.F"
                let (bus_s, devfn_s) = s.split_once(':').ok_or(err)?;
                let (dev_s, fn_s) = devfn_s.split_once('.').ok_or(err)?;
                Ok(Address::new(
                    0,
                    u8::from_str_radix(bus_s, 16).map_err(|_| err)?,
                    u8::from_str_radix(dev_s, 16).map_err(|_| err)?,
                    u8::from_str_radix(fn_s, 16).map_err(|_| err)?,
                ))
            }
            _ => Err(err),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn roundtrip_full() {
        let addr = Address::new(0, 3, 0, 0);
        assert_eq!(addr.to_string(), "0000:03:00.0");
        let parsed: Address = "0000:03:00.0".parse().unwrap();
        assert_eq!(parsed, addr);
    }

    #[test]
    fn short_form() {
        let addr: Address = "03:00.0".parse().unwrap();
        assert_eq!(addr, Address::new(0, 3, 0, 0));
    }

    #[test]
    fn accessors() {
        let addr = Address::new(0x0001, 0x02, 0x03, 0x04);
        assert_eq!(addr.domain(), 0x0001);
        assert_eq!(addr.bus(), 0x02);
        assert_eq!(addr.device(), 0x03);
        assert_eq!(addr.function(), 0x04);
    }

    #[test]
    fn invalid() {
        assert!("not-an-address".parse::<Address>().is_err());
        assert!("".parse::<Address>().is_err());
        assert!("::".parse::<Address>().is_err());
    }
}
