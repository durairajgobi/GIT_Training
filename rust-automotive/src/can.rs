//! Classic CAN 2.0 frames and little-endian (Intel) signal packing, as described in DBC files.

use std::fmt;

pub const MAX_STANDARD_ID: u32 = 0x7FF;
pub const MAX_EXTENDED_ID: u32 = 0x1FFF_FFFF;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CanId {
    Standard(u16),
    Extended(u32),
}

impl CanId {
    pub fn raw(self) -> u32 {
        match self {
            CanId::Standard(id) => id as u32,
            CanId::Extended(id) => id,
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum CanError {
    InvalidId(u32),
    PayloadTooLong(usize),
    SignalOutOfRange { start_bit: u8, length: u8 },
    ValueOutOfRange,
}

impl fmt::Display for CanError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            CanError::InvalidId(id) => write!(f, "invalid CAN identifier 0x{id:X}"),
            CanError::PayloadTooLong(n) => write!(f, "payload of {n} bytes exceeds 8"),
            CanError::SignalOutOfRange { start_bit, length } => {
                write!(
                    f,
                    "signal at bit {start_bit} with length {length} does not fit in 64 bits"
                )
            }
            CanError::ValueOutOfRange => write!(f, "physical value outside signal range"),
        }
    }
}

impl std::error::Error for CanError {}

/// A classic CAN data frame with up to 8 bytes of payload.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct CanFrame {
    id: CanId,
    dlc: u8,
    data: [u8; 8],
}

impl CanFrame {
    pub fn new(id: CanId, payload: &[u8]) -> Result<Self, CanError> {
        match id {
            CanId::Standard(raw) if raw as u32 > MAX_STANDARD_ID => {
                return Err(CanError::InvalidId(raw as u32))
            }
            CanId::Extended(raw) if raw > MAX_EXTENDED_ID => return Err(CanError::InvalidId(raw)),
            _ => {}
        }
        if payload.len() > 8 {
            return Err(CanError::PayloadTooLong(payload.len()));
        }
        let mut data = [0u8; 8];
        data[..payload.len()].copy_from_slice(payload);
        Ok(Self {
            id,
            dlc: payload.len() as u8,
            data,
        })
    }

    pub fn id(&self) -> CanId {
        self.id
    }

    pub fn data(&self) -> &[u8] {
        &self.data[..self.dlc as usize]
    }

    fn as_u64(&self) -> u64 {
        u64::from_le_bytes(self.data)
    }
}

impl fmt::Display for CanFrame {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self.id {
            CanId::Standard(id) => write!(f, "{id:03X}")?,
            CanId::Extended(id) => write!(f, "{id:08X}")?,
        }
        write!(f, " [{}]", self.dlc)?;
        for b in self.data() {
            write!(f, " {b:02X}")?;
        }
        Ok(())
    }
}

/// A DBC-style signal definition: `physical = raw * factor + offset`.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Signal {
    pub start_bit: u8,
    pub length: u8,
    pub factor: f64,
    pub offset: f64,
}

impl Signal {
    fn check(&self) -> Result<u64, CanError> {
        if self.length == 0 || self.length > 64 || self.start_bit as u16 + self.length as u16 > 64 {
            return Err(CanError::SignalOutOfRange {
                start_bit: self.start_bit,
                length: self.length,
            });
        }
        Ok(if self.length == 64 {
            u64::MAX
        } else {
            (1u64 << self.length) - 1
        })
    }

    pub fn decode(&self, frame: &CanFrame) -> Result<f64, CanError> {
        let mask = self.check()?;
        let raw = (frame.as_u64() >> self.start_bit) & mask;
        Ok(raw as f64 * self.factor + self.offset)
    }

    pub fn encode(&self, frame: &mut CanFrame, physical: f64) -> Result<(), CanError> {
        let mask = self.check()?;
        let raw = ((physical - self.offset) / self.factor).round();
        if raw < 0.0 || raw > mask as f64 {
            return Err(CanError::ValueOutOfRange);
        }
        let bits = (frame.as_u64() & !(mask << self.start_bit)) | ((raw as u64) << self.start_bit);
        frame.data = bits.to_le_bytes();
        let used_bytes = (self.start_bit + self.length).div_ceil(8);
        frame.dlc = frame.dlc.max(used_bytes);
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rejects_invalid_frames() {
        assert_eq!(
            CanFrame::new(CanId::Standard(0x800), &[]),
            Err(CanError::InvalidId(0x800))
        );
        assert_eq!(
            CanFrame::new(CanId::Standard(0x100), &[0; 9]),
            Err(CanError::PayloadTooLong(9))
        );
    }

    #[test]
    fn signal_round_trip() {
        let speed = Signal {
            start_bit: 12,
            length: 13,
            factor: 0.05,
            offset: 0.0,
        };
        let mut frame = CanFrame::new(CanId::Standard(0x1A0), &[0xFF]).unwrap();
        speed.encode(&mut frame, 123.45).unwrap();
        assert!((speed.decode(&frame).unwrap() - 123.45).abs() < 1e-9);
        assert_eq!(
            frame.data()[0],
            0xFF,
            "bits outside the signal are preserved"
        );
        assert_eq!(frame.data().len(), 4);
    }

    #[test]
    fn encode_rejects_out_of_range() {
        let temp = Signal {
            start_bit: 0,
            length: 8,
            factor: 1.0,
            offset: -40.0,
        };
        let mut frame = CanFrame::new(CanId::Standard(0x100), &[]).unwrap();
        assert_eq!(
            temp.encode(&mut frame, 300.0),
            Err(CanError::ValueOutOfRange)
        );
        assert_eq!(
            temp.encode(&mut frame, -50.0),
            Err(CanError::ValueOutOfRange)
        );
    }

    #[test]
    fn display_format() {
        let frame = CanFrame::new(CanId::Standard(0x7DF), &[0x02, 0x01, 0x0C]).unwrap();
        assert_eq!(frame.to_string(), "7DF [3] 02 01 0C");
    }
}
