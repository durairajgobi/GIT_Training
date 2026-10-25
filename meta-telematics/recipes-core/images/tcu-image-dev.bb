SUMMARY = "Telematics control unit lab/debug image"
require tcu-image.bb

IMAGE_FEATURES += "debug-tweaks tools-debug tools-profile"
IMAGE_FEATURES:remove = "read-only-rootfs"

# Telnet exists only here, for bench access to units without SSH keys;
# it is never part of tcu-image.
IMAGE_INSTALL:append = " inetutils-telnetd tcpdump iperf3 strace"
