SUMMARY = "blemgrd - BLE connection manager (BlueZ D-Bus, C++17)"
DESCRIPTION = "Keeps BLE peripherals connected (scan, serialized connect, \
auto-reconnect, persistent notifications, pairing agent) and optionally \
exposes a GATT server with LE advertising."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "git://github.com/durairajgobi/GIT_Training.git;protocol=https;branch=main"
SRCREV ?= "${AUTOREV}"
PV = "1.0+git${SRCPV}"
S = "${WORKDIR}/git"
OECMAKE_SOURCEPATH = "${S}/ble_manager"

DEPENDS = "glib-2.0"

inherit cmake pkgconfig systemd useradd

EXTRA_OECMAKE = "-DBLEMGR_BUILD_TESTS=OFF"

USERADD_PACKAGES = "${PN}"
GROUPADD_PARAM:${PN} = "--system bluetooth"
USERADD_PARAM:${PN} = "--system --no-create-home --home-dir /nonexistent \
    --shell /sbin/nologin --groups bluetooth blemgr"

SYSTEMD_SERVICE:${PN} = "blemgrd.service"
SYSTEMD_AUTO_ENABLE = "enable"

do_install:append() {
    # CMake installs below ${prefix}; systemd.bbclass expects the unit here.
    rm -rf ${D}${prefix}/lib/systemd
    install -D -m 0644 ${OECMAKE_SOURCEPATH}/systemd/blemgrd.service \
        ${D}${systemd_system_unitdir}/blemgrd.service
}

FILES:${PN} += "${systemd_system_unitdir} ${datadir}/dbus-1/system.d"
CONFFILES:${PN} = "${sysconfdir}/blemgr/blemgrd.conf"
RDEPENDS:${PN} = "bluez5"
