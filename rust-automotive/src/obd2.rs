//! OBD-II (SAE J1979) Mode 01 "show current data" over ISO 15765-4 CAN (11-bit IDs).

use crate::can::{CanError, CanFrame, CanId};

/// Functional (broadcast) request address.
pub const FUNCTIONAL_REQUEST_ID: u16 = 0x7DF;
/// ECU responses use 0x7E8..=0x7EF.
pub const RESPONSE_IDS: std::ops::RangeInclusive<u16> = 0x7E8..=0x7EF;

const MODE_CURRENT_DATA: u8 = 0x01;
const POSITIVE_RESPONSE_OFFSET: u8 = 0x40;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Pid {
    EngineLoad = 0x04,
    CoolantTemp = 0x05,
    EngineRpm = 0x0C,
    VehicleSpeed = 0x0D,
    ThrottlePosition = 0x11,
    FuelLevel = 0x2F,
}

impl Pid {
    pub fn from_u8(v: u8) -> Option<Self> {
        Some(match v {
            0x04 => Pid::EngineLoad,
            0x05 => Pid::CoolantTemp,
            0x0C => Pid::EngineRpm,
            0x0D => Pid::VehicleSpeed,
            0x11 => Pid::ThrottlePosition,
            0x2F => Pid::FuelLevel,
            _ => return None,
        })
    }

    fn data_len(self) -> usize {
        match self {
            Pid::EngineRpm => 2,
            _ => 1,
        }
    }

    pub fn unit(self) -> &'static str {
        match self {
            Pid::CoolantTemp => "°C",
            Pid::EngineRpm => "rpm",
            Pid::VehicleSpeed => "km/h",
            Pid::EngineLoad | Pid::ThrottlePosition | Pid::FuelLevel => "%",
        }
    }

    fn decode(self, d: &[u8]) -> f64 {
        let a = d[0] as f64;
        match self {
            Pid::EngineLoad | Pid::ThrottlePosition | Pid::FuelLevel => a * 100.0 / 255.0,
            Pid::CoolantTemp => a - 40.0,
            Pid::EngineRpm => (256.0 * a + d[1] as f64) / 4.0,
            Pid::VehicleSpeed => a,
        }
    }
}

#[derive(Debug, Clone, PartialEq)]
pub enum ObdError {
    NotAResponse(u32),
    Malformed,
    UnexpectedMode(u8),
    UnsupportedPid(u8),
}

/// Builds a single-frame ISO-TP request for a Mode 01 PID.
pub fn request(pid: Pid) -> Result<CanFrame, CanError> {
    CanFrame::new(
        CanId::Standard(FUNCTIONAL_REQUEST_ID),
        &[
            0x02,
            MODE_CURRENT_DATA,
            pid as u8,
            0x55,
            0x55,
            0x55,
            0x55,
            0x55,
        ],
    )
}

/// Parses a single-frame ECU response into a PID and physical value.
pub fn parse_response(frame: &CanFrame) -> Result<(Pid, f64), ObdError> {
    match frame.id() {
        CanId::Standard(id) if RESPONSE_IDS.contains(&id) => {}
        other => return Err(ObdError::NotAResponse(other.raw())),
    }
    let d = frame.data();
    // Byte 0 is the ISO-TP single-frame PCI: high nibble 0, low nibble = length.
    let len = *d.first().ok_or(ObdError::Malformed)? as usize;
    if !(2..=7).contains(&len) || d.len() < len + 1 {
        return Err(ObdError::Malformed);
    }
    if d[1] != MODE_CURRENT_DATA + POSITIVE_RESPONSE_OFFSET {
        return Err(ObdError::UnexpectedMode(d[1]));
    }
    let pid = Pid::from_u8(d[2]).ok_or(ObdError::UnsupportedPid(d[2]))?;
    let payload = &d[3..=len];
    if payload.len() < pid.data_len() {
        return Err(ObdError::Malformed);
    }
    Ok((pid, pid.decode(payload)))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn response(bytes: &[u8]) -> CanFrame {
        CanFrame::new(CanId::Standard(0x7E8), bytes).unwrap()
    }

    #[test]
    fn builds_request() {
        let f = request(Pid::EngineRpm).unwrap();
        assert_eq!(f.id(), CanId::Standard(0x7DF));
        assert_eq!(&f.data()[..3], &[0x02, 0x01, 0x0C]);
    }

    #[test]
    fn decodes_rpm_and_temp() {
        assert_eq!(
            parse_response(&response(&[0x04, 0x41, 0x0C, 0x1A, 0xF8])).unwrap(),
            (Pid::EngineRpm, 1726.0)
        );
        assert_eq!(
            parse_response(&response(&[0x03, 0x41, 0x05, 0x7B])).unwrap(),
            (Pid::CoolantTemp, 83.0)
        );
    }

    #[test]
    fn rejects_bad_responses() {
        let wrong_id = CanFrame::new(CanId::Standard(0x123), &[0x03, 0x41, 0x0D, 0x10]).unwrap();
        assert_eq!(
            parse_response(&wrong_id),
            Err(ObdError::NotAResponse(0x123))
        );
        assert_eq!(
            parse_response(&response(&[0x03, 0x7F, 0x01, 0x12])),
            Err(ObdError::UnexpectedMode(0x7F))
        );
        assert_eq!(
            parse_response(&response(&[0x03, 0x41, 0x0C, 0x1A])),
            Err(ObdError::Malformed)
        );
        assert_eq!(
            parse_response(&response(&[0x03, 0x41, 0xA6, 0x00])),
            Err(ObdError::UnsupportedPid(0xA6))
        );
    }
}
