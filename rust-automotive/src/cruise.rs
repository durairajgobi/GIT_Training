//! Cruise control: a PI speed controller with output clamping, anti-windup and
//! the usual driver-override disengage conditions.

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Gains {
    pub kp: f64,
    pub ki: f64,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Mode {
    Off,
    Standby,
    Active,
}

/// Driver inputs and vehicle state sampled each control cycle.
#[derive(Debug, Clone, Copy, Default)]
pub struct Inputs {
    pub speed_kph: f64,
    pub brake_pressed: bool,
    pub clutch_pressed: bool,
}

/// Throttle/brake request in `-1.0..=1.0` (negative = braking).
pub type Command = f64;

pub const MIN_SET_SPEED_KPH: f64 = 30.0;
pub const MAX_SET_SPEED_KPH: f64 = 180.0;

#[derive(Debug)]
pub struct CruiseControl {
    gains: Gains,
    mode: Mode,
    set_speed: f64,
    integral: f64,
}

impl CruiseControl {
    pub fn new(gains: Gains) -> Self {
        Self {
            gains,
            mode: Mode::Off,
            set_speed: 0.0,
            integral: 0.0,
        }
    }

    pub fn mode(&self) -> Mode {
        self.mode
    }

    pub fn set_speed(&self) -> f64 {
        self.set_speed
    }

    pub fn power(&mut self, on: bool) {
        self.mode = if on { Mode::Standby } else { Mode::Off };
        self.integral = 0.0;
    }

    /// "SET" button: engage at the current speed if it is in the allowed range.
    pub fn engage(&mut self, current_speed: f64) -> bool {
        if self.mode == Mode::Off
            || !(MIN_SET_SPEED_KPH..=MAX_SET_SPEED_KPH).contains(&current_speed)
        {
            return false;
        }
        self.set_speed = current_speed;
        self.integral = 0.0;
        self.mode = Mode::Active;
        true
    }

    /// "+" / "-" buttons.
    pub fn adjust(&mut self, delta_kph: f64) {
        if self.mode == Mode::Active {
            self.set_speed =
                (self.set_speed + delta_kph).clamp(MIN_SET_SPEED_KPH, MAX_SET_SPEED_KPH);
        }
    }

    pub fn step(&mut self, input: Inputs, dt_s: f64) -> Command {
        if self.mode != Mode::Active {
            return 0.0;
        }
        if input.brake_pressed || input.clutch_pressed {
            self.mode = Mode::Standby;
            self.integral = 0.0;
            return 0.0;
        }

        let error = self.set_speed - input.speed_kph;
        let candidate = self.integral + error * dt_s;
        let unclamped = self.gains.kp * error + self.gains.ki * candidate;
        let output = unclamped.clamp(-1.0, 1.0);
        // Conditional integration: only accumulate while not saturated,
        // or when the error would pull the output back out of saturation.
        if output == unclamped || unclamped.signum() != error.signum() {
            self.integral = candidate;
        }
        output
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn controller() -> CruiseControl {
        let mut cc = CruiseControl::new(Gains { kp: 0.08, ki: 0.02 });
        cc.power(true);
        cc
    }

    #[test]
    fn engage_rules() {
        let mut cc = CruiseControl::new(Gains { kp: 0.1, ki: 0.0 });
        assert!(!cc.engage(100.0), "cannot engage while off");
        cc.power(true);
        assert!(!cc.engage(20.0), "too slow");
        assert!(cc.engage(100.0));
        cc.adjust(200.0);
        assert_eq!(cc.set_speed(), MAX_SET_SPEED_KPH);
    }

    #[test]
    fn brake_disengages() {
        let mut cc = controller();
        cc.engage(90.0);
        let cmd = cc.step(
            Inputs {
                speed_kph: 90.0,
                brake_pressed: true,
                ..Default::default()
            },
            0.1,
        );
        assert_eq!(cmd, 0.0);
        assert_eq!(cc.mode(), Mode::Standby);
    }

    #[test]
    fn converges_on_simple_vehicle_model() {
        let mut cc = controller();
        cc.engage(80.0);
        cc.adjust(20.0);
        let mut speed = 80.0;
        for _ in 0..3000 {
            let cmd = cc.step(
                Inputs {
                    speed_kph: speed,
                    ..Default::default()
                },
                0.1,
            );
            assert!((-1.0..=1.0).contains(&cmd));
            // Max acceleration ~2 m/s² (7.2 km/h/s), drag proportional to speed.
            speed += (cmd * 7.2 - 0.03 * speed) * 0.1;
        }
        assert!((speed - 100.0).abs() < 0.5, "speed settled at {speed}");
    }
}
