SUMMARY = "Docker compose stack for TCU containerised workloads"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://docker-compose.yml file://tcu-containers.service"
S = "${WORKDIR}"

inherit allarch systemd

SYSTEMD_SERVICE:${PN} = "tcu-containers.service"

do_install() {
    install -Dm 0644 ${S}/docker-compose.yml ${D}${sysconfdir}/tcu/containers/docker-compose.yml
    install -Dm 0644 ${S}/tcu-containers.service ${D}${systemd_system_unitdir}/tcu-containers.service
    # Image tarballs are added by the OTA bundle build, not by this recipe.
    install -d ${D}${datadir}/tcu/images
}

FILES:${PN} += "${systemd_system_unitdir} ${datadir}/tcu/images"
RDEPENDS:${PN} = "docker-moby docker-compose"
