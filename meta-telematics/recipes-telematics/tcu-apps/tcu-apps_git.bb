SUMMARY = "TCU services: eCall, LTE connection manager, Bluetooth, power, hotspot, security tools"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "git://github.com/durairajgobi/GIT_Training.git;protocol=https;branch=main"
# Pin a release commit for production builds; AUTOREV is for integration.
SRCREV ?= "${AUTOREV}"
PV = "1.0+git${SRCPV}"
S = "${WORKDIR}/git"

DEPENDS = "openssl glib-2.0"

inherit cmake pkgconfig systemd useradd

EXTRA_OECMAKE = "-DTCU_BUILD_TESTS=OFF"

USERADD_PACKAGES = "${PN}"
GROUPADD_PARAM:${PN} = "--system ecall"
USERADD_PARAM:${PN} = "--system --no-create-home --home-dir /nonexistent \
    --shell /sbin/nologin --gid ecall ecall"

SYSTEMD_SERVICE:${PN} = "ecalld.service ltecm.service btmgr.service \
    tcu-pmd.service tcu-hotspot.service"
SYSTEMD_AUTO_ENABLE = "enable"

do_install:append() {
    # Secrets are provisioned at end-of-line, never shipped in the image.
    chmod 0640 ${D}${sysconfdir}/tcu/hostapd-ap0.conf
}

FILES:${PN} += "${systemd_system_unitdir} ${libexecdir}/tcu-hotspot ${sysconfdir}/tcu"
CONFFILES:${PN} = "${sysconfdir}/tcu/vehicle.env ${sysconfdir}/tcu/hotspot-policy.json"

RDEPENDS:${PN} = "bluez5 hostapd dnsmasq iw nftables iproute2 \
    python3-core python3-json python3-netclient \
    busybox-udhcpc"
