//! Example automotive software components written in safe Rust.
//!
//! * [`can`]    – classic CAN frames and DBC-style signal packing
//! * [`obd2`]   – OBD-II (SAE J1979) Mode 01 request/response handling
//! * [`bms`]    – battery management: cell monitoring, faults, state of charge
//! * [`cruise`] – cruise control PI controller with anti-windup

pub mod bms;
pub mod can;
pub mod cruise;
pub mod obd2;
