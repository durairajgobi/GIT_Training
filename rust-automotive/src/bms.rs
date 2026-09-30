//! A simplified battery management system (BMS) for a lithium-ion pack.
//!
//! Monitors cell voltages and temperatures, latches faults, and estimates
//! state of charge (SoC) by coulomb counting.

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Limits {
    pub cell_min_v: f64,
    pub cell_max_v: f64,
    pub temp_max_c: f64,
    pub temp_min_charge_c: f64,
    pub max_imbalance_v: f64,
}

impl Default for Limits {
    fn default() -> Self {
        Self {
            cell_min_v: 2.8,
            cell_max_v: 4.2,
            temp_max_c: 60.0,
            temp_min_charge_c: 0.0,
            max_imbalance_v: 0.1,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum Fault {
    UnderVoltage { cell: usize, volts: f64 },
    OverVoltage { cell: usize, volts: f64 },
    OverTemperature { celsius: f64 },
    ChargeTooCold { celsius: f64 },
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum State {
    Idle,
    Charging,
    Discharging,
    /// Contactors open; stays latched until [`Bms::clear_fault`] succeeds.
    Fault(Fault),
}

/// One measurement cycle. Positive `current_a` means discharge.
#[derive(Debug, Clone)]
pub struct Sample<'a> {
    pub cell_voltages: &'a [f64],
    pub temperature_c: f64,
    pub current_a: f64,
    pub dt_s: f64,
}

#[derive(Debug)]
pub struct Bms {
    limits: Limits,
    capacity_ah: f64,
    soc: f64,
    state: State,
    balancing: Vec<bool>,
}

const CURRENT_DEADBAND_A: f64 = 0.5;

impl Bms {
    pub fn new(limits: Limits, capacity_ah: f64, initial_soc: f64) -> Self {
        Self {
            limits,
            capacity_ah,
            soc: initial_soc.clamp(0.0, 1.0),
            state: State::Idle,
            balancing: Vec::new(),
        }
    }

    pub fn state(&self) -> State {
        self.state
    }

    /// State of charge in the range `0.0..=1.0`.
    pub fn soc(&self) -> f64 {
        self.soc
    }

    /// Cells currently being passively discharged to balance the pack.
    pub fn balancing(&self) -> &[bool] {
        &self.balancing
    }

    pub fn contactors_closed(&self) -> bool {
        !matches!(self.state, State::Fault(_))
    }

    pub fn update(&mut self, s: &Sample) -> State {
        if let State::Fault(_) = self.state {
            self.balancing.iter_mut().for_each(|b| *b = false);
            return self.state;
        }

        if let Some(fault) = self.check(s) {
            self.state = State::Fault(fault);
            self.balancing = vec![false; s.cell_voltages.len()];
            return self.state;
        }

        self.soc = (self.soc - s.current_a * s.dt_s / 3600.0 / self.capacity_ah).clamp(0.0, 1.0);
        self.state = if s.current_a > CURRENT_DEADBAND_A {
            State::Discharging
        } else if s.current_a < -CURRENT_DEADBAND_A {
            State::Charging
        } else {
            State::Idle
        };

        let min = s
            .cell_voltages
            .iter()
            .copied()
            .fold(f64::INFINITY, f64::min);
        self.balancing = s
            .cell_voltages
            .iter()
            .map(|&v| self.state == State::Charging && v - min > self.limits.max_imbalance_v)
            .collect();

        self.state
    }

    /// Attempts to leave the fault state; succeeds only if `s` is within limits.
    pub fn clear_fault(&mut self, s: &Sample) -> bool {
        if matches!(self.state, State::Fault(_)) && self.check(s).is_none() {
            self.state = State::Idle;
        }
        !matches!(self.state, State::Fault(_))
    }

    fn check(&self, s: &Sample) -> Option<Fault> {
        let l = &self.limits;
        for (cell, &volts) in s.cell_voltages.iter().enumerate() {
            if volts < l.cell_min_v {
                return Some(Fault::UnderVoltage { cell, volts });
            }
            if volts > l.cell_max_v {
                return Some(Fault::OverVoltage { cell, volts });
            }
        }
        if s.temperature_c > l.temp_max_c {
            return Some(Fault::OverTemperature {
                celsius: s.temperature_c,
            });
        }
        if s.current_a < -CURRENT_DEADBAND_A && s.temperature_c < l.temp_min_charge_c {
            return Some(Fault::ChargeTooCold {
                celsius: s.temperature_c,
            });
        }
        None
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample(cells: &[f64], temp: f64, current: f64) -> Sample<'_> {
        Sample {
            cell_voltages: cells,
            temperature_c: temp,
            current_a: current,
            dt_s: 3600.0,
        }
    }

    #[test]
    fn coulomb_counting() {
        let mut bms = Bms::new(Limits::default(), 50.0, 0.8);
        assert_eq!(
            bms.update(&sample(&[3.7; 4], 25.0, 10.0)),
            State::Discharging
        );
        assert!((bms.soc() - 0.6).abs() < 1e-9);
        assert_eq!(bms.update(&sample(&[3.7; 4], 25.0, -25.0)), State::Charging);
        assert!((bms.soc() - 1.0).abs() < 1e-9);
        assert_eq!(bms.update(&sample(&[3.7; 4], 25.0, -25.0)), State::Charging);
        assert_eq!(bms.soc(), 1.0, "SoC is clamped");
    }

    #[test]
    fn faults_latch_until_cleared() {
        let mut bms = Bms::new(Limits::default(), 50.0, 0.5);
        let state = bms.update(&sample(&[3.7, 4.3, 3.7], 25.0, 0.0));
        assert_eq!(
            state,
            State::Fault(Fault::OverVoltage {
                cell: 1,
                volts: 4.3
            })
        );
        assert!(!bms.contactors_closed());

        // Healthy sample does not auto-recover.
        assert!(matches!(
            bms.update(&sample(&[3.7; 3], 25.0, 0.0)),
            State::Fault(_)
        ));
        assert!(!bms.clear_fault(&sample(&[3.7; 3], 70.0, 0.0)));
        assert!(bms.clear_fault(&sample(&[3.7; 3], 25.0, 0.0)));
        assert!(bms.contactors_closed());
    }

    #[test]
    fn cold_charging_is_blocked() {
        let mut bms = Bms::new(Limits::default(), 50.0, 0.5);
        assert_eq!(
            bms.update(&sample(&[3.7; 2], -5.0, 5.0)),
            State::Discharging
        );
        assert_eq!(
            bms.update(&sample(&[3.7; 2], -5.0, -5.0)),
            State::Fault(Fault::ChargeTooCold { celsius: -5.0 })
        );
    }

    #[test]
    fn balances_high_cells_while_charging() {
        let mut bms = Bms::new(Limits::default(), 50.0, 0.5);
        bms.update(&sample(&[3.90, 4.05, 3.95], 25.0, -10.0));
        assert_eq!(bms.balancing(), &[false, true, false]);
        bms.update(&sample(&[3.90, 4.05, 3.95], 25.0, 10.0));
        assert_eq!(bms.balancing(), &[false, false, false]);
    }
}
