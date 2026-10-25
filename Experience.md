# Professional Experience — Embedded Linux / Telematics

## Platform & SoC
- Benchmarked candidate application processors and drove final SoC selection for the telematics product.
- Owned Embedded Linux software design and development on the NXP i.MX8 application processor.
- Handled hardware customization and peripheral interfacing for the platform.
- Designed Linux power management mechanisms for the platform.

## Yocto & Boot
- Built a customized Linux distribution using the Yocto Project.
- Built and integrated telematics applications (eCall, Bluetooth, Wi-Fi) through the Yocto build system.
- Customized the boot process and ported OP-TEE (ARM TrustZone) to i.MX6.
- Implemented platform virtualization using Docker containers.

## Security
- Enabled the platform security layer in Yocto using the Linux kernel crypto API, OpenSSL and OpenSSH.
- Implemented TLS/SSL, PKI and OCSP certificate validation for secure telematics communication.
- Integrated AAA services (RADIUS, TACACS+, LDAP) for device and operator authentication.

## Connectivity
- Designed and developed the LTE connectivity stack.
- Implemented networking protocols (SCTP, TCP/IP, SSH, Telnet) for telematics features.
- Ported and hardened BlueZ 5.x for the telematics unit, adding HFP 1.7 wideband speech and
  reconnection state-machine fixes that reduced field pairing complaints by ~60%.
- Delivered an in-truck Wi-Fi hotspot (SoftAP + LTE tethering) with per-passenger data policy,
  captive-portal onboarding and WPA3 migration.

## Test Automation
- Built a Python/pytest framework that drives the telematics unit, secondary head unit,
  programmable APs and sniffers; runs 300 connect, roam, hotspot and stress cases nightly in
  Jenkins, cutting regression effort by 70%.

## Skills
- **Languages:** C, C++, Python, Shell
- **Platforms:** NXP i.MX8, i.MX6, ARM TrustZone / OP-TEE
- **Build & OS:** Yocto Project, Embedded Linux, Docker
- **Connectivity:** LTE, BlueZ 5.x (HFP 1.7), Wi-Fi (SoftAP, WPA3), SCTP, TCP/IP
- **Security:** OpenSSL, OpenSSH, Linux kernel crypto API, TLS, PKI, OCSP, RADIUS, TACACS+, LDAP
- **Testing & CI:** pytest, Jenkins
