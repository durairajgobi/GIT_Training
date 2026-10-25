#!/bin/sh
# SPDX-License-Identifier: MIT
# End-of-line PKI provisioning (run on the TCU over the factory link).
#   1. generate the identity key inside OP-TEE (never exported)
#   2. print the public key; the factory station builds the CSR, signs it
#      with the key via `tcu-keystore sign`, and gets it issued by the
#      fleet device CA
#   3. install the issued certificate chain
#   provision_device_cert.sh generate
#   provision_device_cert.sh install <chain.pem>
set -eu
CERT_DIR=/etc/tcu/pki

case "${1:-}" in
generate)
	tcu-keystore generate device
	tcu-keystore pubkey device
	;;
install)
	install -d -m 0750 "$CERT_DIR"
	# Leaf must match the TEE-held key and chain to the fleet root
	openssl verify -CAfile "$CERT_DIR/fleet-root-ca.pem" -untrusted "$2" "$2"
	install -m 0644 "$2" "$CERT_DIR/device-chain.pem"
	;;
*)
	echo "usage: $0 generate | install <chain.pem>" >&2
	exit 2
	;;
esac
