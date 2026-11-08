#!/usr/bin/env bash
# Runs ble-app against fake_bluez on a private D-Bus daemon.
#   tests/integration.sh <build-dir>
set -euo pipefail
BUILD=${1:-build}
WORK=$(mktemp -d)
trap 'kill ${FAKE_PID:-} ${BUS_PID:-} 2>/dev/null || true; rm -rf "$WORK"' EXIT

cat > "$WORK/bus.conf" <<CONF
<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-BUS Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig>
  <type>system</type>
  <listen>unix:path=$WORK/bus</listen>
  <auth>EXTERNAL</auth>
  <policy context="default">
    <allow user="*"/><allow own="*"/><allow send_type="method_call"/>
    <allow send_destination="*" eavesdrop="true"/><allow receive_sender="*"/>
  </policy>
</busconfig>
CONF
dbus-daemon --config-file="$WORK/bus.conf" --nofork --nopidfile & BUS_PID=$!
export DBUS_SYSTEM_BUS_ADDRESS="unix:path=$WORK/bus"
for _ in $(seq 50); do [ -S "$WORK/bus" ] && break; sleep 0.1; done

"$BUILD/fake_bluez" > "$WORK/fake.log" 2>&1 & FAKE_PID=$!
for _ in $(seq 50); do grep -q ready "$WORK/fake.log" && break; sleep 0.1; done

fail() { echo "FAIL: $*"; echo "--- output"; cat "$WORK/out" || true; echo "--- fake bluez"; cat "$WORK/fake.log"; exit 1; }
run() { timeout 20 "$BUILD/ble-app" "$@" > "$WORK/out" 2>&1 || fail "ble-app $* exited $?"; cat "$WORK/out"; }
expect() { grep -q -- "$1" "$WORK/out" || fail "expected '$1'"; }
expect_fake() { grep -q -- "$1" "$WORK/fake.log" || fail "expected fake bluez to log '$1'"; }

echo "== scan";    run scan -t 1 --uuid 180f
expect "NEW   11:22:33:44:55:66"; expect "1 device(s) found"; expect_fake "Adapter1.SetDiscoveryFilter"
expect_fake "set /org/bluez/hci0.Powered"; expect_fake "capability KeyboardDisplay"

echo "== connect"; run connect 11:22:33:44:55:66
expect "READY"; expect "Battery Service"; expect "Battery Level"; expect "\[read,write,notify\]"
expect_fake "Device1.Pair"; expect "confirm passkey 123456"; expect_fake "set /org/bluez/hci0/dev_11_22_33_44_55_66.Trusted"

echo "== read";    run read 11:22:33:44:55:66 2a19;            expect "00002a19-0000-1000-8000-00805f9b34fb: 55"
echo "== write";   run write 11:22:33:44:55:66 2a19 "de ad";   expect "wrote 2 byte(s)"
echo "== read2";   run read 11:22:33:44:55:66 2a19;            expect ": de ad"
echo "== notify";  run notify 11:22:33:44:55:66 2a19 -t 1;     expect ": 5a"; expect ": 59"
echo "== disconnect"; run disconnect 11:22:33:44:55:66; expect "disconnected"
echo "== auto";    run auto -t 5 --max 1 --name Fake
expect "UP    11:22:33:44:55:66"; expect "connected: 1, failed: 0"
echo "== serve"
timeout -s INT 2 "$BUILD/ble-app" serve --name TestPeripheral > "$WORK/out" 2>&1 || true
cat "$WORK/out"
expect "GATT application registered"; expect "Advertising as \"TestPeripheral\""
expect_fake "application has 3 service(s), 5 characteristic(s)"
expect_fake "LocalName.*TestPeripheral"; expect_fake "capability NoInputNoOutput"
expect_fake "UnregisterAdvertisement"; expect_fake "UnregisterApplication"
grep -E "advertisement|app characteristic" "$WORK/fake.log"
echo "integration: all passed"
