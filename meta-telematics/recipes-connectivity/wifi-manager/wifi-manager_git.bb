SUMMARY = "Wi-Fi station manager (wifimgrd daemon and wifictl CLI)"
DESCRIPTION = "Drives wpa_supplicant over its control socket: saved networks, \
priority/signal based selection, failure backoff, roaming between SSIDs, \
DHCP supervision and a local control socket."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "git://github.com/durairajgobi/GIT_Training.git;protocol=https;branch=main"
# Pin a release commit for production builds; AUTOREV is for integration.
SRCREV ?= "${AUTOREV}"
PV = "1.0+git${SRCPV}"
S = "${WORKDIR}/git/wifi_manager"

inherit cmake systemd

EXTRA_OECMAKE = "-DWIFIMGR_BUILD_TESTS=OFF -DWIFIMGR_WARNINGS_AS_ERRORS=ON"

SYSTEMD_SERVICE:${PN} = "wifimgrd.service"
SYSTEMD_AUTO_ENABLE = "enable"

do_install:append() {
    install -d -m 0700 ${D}${localstatedir}/lib/wifimgr
}

FILES:${PN} += "${systemd_system_unitdir} ${libexecdir}/wifimgr ${localstatedir}/lib/wifimgr"
CONFFILES:${PN} = "${sysconfdir}/wifimgr/wifimgr.conf \
    ${sysconfdir}/wpa_supplicant/wpa_supplicant-wlan0.conf"

RDEPENDS:${PN} = "wpa-supplicant busybox-udhcpc iproute2"
