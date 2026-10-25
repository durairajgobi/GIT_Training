#!/bin/sh
# SPDX-License-Identifier: MIT
# Flash the bench TCU: power-cycle into serial download mode, write the
# image with uuu, boot, and wait until SSH answers.
#   scripts/flash_bench.sh <image.wic.zst>
set -eu
IMAGE=$1
RELAY=${RELAY:-/usr/local/bin/bench-relay}
DUT=${DUT:-root@10.88.0.1}

"$RELAY" boot-mode serial
"$RELAY" power-cycle
uuu -b emmc_all imx-boot-tcu.bin-flash_evk "$IMAGE"
"$RELAY" boot-mode emmc
"$RELAY" power-cycle

for i in $(seq 1 120); do
	if ssh -o BatchMode=yes -o ConnectTimeout=2 "$DUT" true 2>/dev/null; then
		echo "DUT up after ${i}s"
		exit 0
	fi
	sleep 1
done
echo "DUT did not come up" >&2
exit 1
