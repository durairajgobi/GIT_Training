//! Simulates a short drive: cruise control drives a simple vehicle model, the
//! BMS tracks the traction battery, and values are published on a virtual CAN
//! bus and queried back through OBD-II.

use rust_automotive::bms::{Bms, Limits, Sample, State};
use rust_automotive::can::{CanFrame, CanId, Signal};
use rust_automotive::cruise::{CruiseControl, Gains, Inputs};
use rust_automotive::obd2::{self, Pid};

const SPEED_SIGNAL: Signal = Signal {
    start_bit: 0,
    length: 16,
    factor: 0.01,
    offset: 0.0,
};
const SOC_SIGNAL: Signal = Signal {
    start_bit: 16,
    length: 8,
    factor: 0.5,
    offset: 0.0,
};

fn main() {
    let dt = 0.1;
    let mut speed = 60.0;
    let mut cc = CruiseControl::new(Gains { kp: 0.08, ki: 0.02 });
    let mut bms = Bms::new(Limits::default(), 60.0, 0.85);

    cc.power(true);
    cc.engage(speed);
    cc.adjust(40.0);
    println!("Cruise engaged, target {:.0} km/h\n", cc.set_speed());

    for tick in 0..=600 {
        let cmd = cc.step(
            Inputs {
                speed_kph: speed,
                ..Default::default()
            },
            dt,
        );
        speed += (cmd * 7.2 - 0.03 * speed) * dt;

        // Traction current: propulsion load plus regen when braking.
        let current = 200.0 * cmd + 0.8 * speed;
        let cells = [3.85 - current * 0.0005; 96];
        let state = bms.update(&Sample {
            cell_voltages: &cells,
            temperature_c: 32.0,
            current_a: current,
            dt_s: dt,
        });
        if let State::Fault(f) = state {
            println!("BMS fault: {f:?}");
            break;
        }

        if tick % 100 == 0 {
            let mut frame = CanFrame::new(CanId::Standard(0x3E9), &[]).unwrap();
            SPEED_SIGNAL.encode(&mut frame, speed).unwrap();
            SOC_SIGNAL.encode(&mut frame, bms.soc() * 100.0).unwrap();
            println!(
                "t={:>4.1}s  {frame}  speed={:>6.2} km/h  soc={:>5.1}%  cmd={cmd:+.2}  bms={state:?}",
                tick as f64 * dt,
                SPEED_SIGNAL.decode(&frame).unwrap(),
                SOC_SIGNAL.decode(&frame).unwrap(),
            );
        }
    }

    println!("\nOBD-II scan:");
    let request = obd2::request(Pid::VehicleSpeed).unwrap();
    // Fake ECU answering the functional request.
    let reply = CanFrame::new(
        CanId::Standard(0x7E8),
        &[0x03, 0x41, Pid::VehicleSpeed as u8, speed.round() as u8],
    )
    .unwrap();
    let (pid, value) = obd2::parse_response(&reply).unwrap();
    println!(
        "  -> {request}\n  <- {reply}\n  {pid:?} = {value} {}",
        pid.unit()
    );
}
