SUMMARY = "Telematics control unit production image"
LICENSE = "MIT"

inherit core-image

IMAGE_FEATURES += "ssh-server-openssh read-only-rootfs"

IMAGE_INSTALL:append = " \
    tcu-apps \
    tcu-aaa \
    tcu-containers \
    tcu-keystore-ta \
    bluez5 bluez5-obex \
    wpa-supplicant hostapd dnsmasq iw wireless-regdb-static \
    nftables iproute2 \
    lksctp-tools \
    openssl openssl-bin ca-certificates \
    optee-client \
    docker-moby \
    kernel-module-sctp \
    "

# Keep the image lean; package management is replaced by A/B OTA.
IMAGE_FEATURES:remove = "package-management"
IMAGE_ROOTFS_EXTRA_SPACE = "0"
