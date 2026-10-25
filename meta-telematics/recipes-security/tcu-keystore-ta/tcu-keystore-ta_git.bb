SUMMARY = "OP-TEE trusted application holding the TCU device identity key"
LICENSE = "BSD-2-Clause"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/BSD-2-Clause;md5=cb641bc04cda31daea161b1bc15da69f"

SRC_URI = "git://github.com/durairajgobi/GIT_Training.git;protocol=https;branch=main"
SRCREV ?= "${AUTOREV}"
PV = "1.0+git${SRCPV}"
S = "${WORKDIR}/git/optee"

DEPENDS = "optee-os-tadevkit optee-client python3-cryptography-native"

inherit python3native

TA_UUID = "8d5c3a0e-6f1b-4c2e-9a47-2b1d0c9e7f31"
TA_DEV_KIT_DIR = "${STAGING_INCDIR}/optee/export-user_ta"

EXTRA_OEMAKE = "TA_DEV_KIT_DIR=${TA_DEV_KIT_DIR} CROSS_COMPILE=${TARGET_PREFIX} \
    OPTEE_CLIENT_EXPORT=${STAGING_DIR_HOST}${prefix}"

do_compile() {
    oe_runmake -C ${S}/ta
    oe_runmake -C ${S}/host CC="${CC}" CFLAGS="${CFLAGS} -I${S}/ta/include" LDFLAGS="${LDFLAGS}"
}

do_install() {
    install -Dm 0444 ${S}/ta/${TA_UUID}.ta ${D}${nonarch_base_libdir}/optee_armtz/${TA_UUID}.ta
    install -Dm 0755 ${S}/host/tcu-keystore ${D}${bindir}/tcu-keystore
}

FILES:${PN} += "${nonarch_base_libdir}/optee_armtz"
RDEPENDS:${PN} = "optee-client"
