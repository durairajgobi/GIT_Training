SUMMARY = "AAA for TCU service access: RADIUS, TACACS+ and LDAP via PAM"
DESCRIPTION = "Second factor and authorisation for engineers logging in over SSH. \
RADIUS (OTP) authenticates, TACACS+ authorises and accounts per command, \
LDAP maps operators to the tcu-service group. Falls back to local \
break-glass account only when the backend is unreachable."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://sshd.pam file://pam_radius_auth.conf file://tacplus.conf file://nslcd.conf"
S = "${WORKDIR}"

inherit allarch useradd

USERADD_PACKAGES = "${PN}"
GROUPADD_PARAM:${PN} = "--system tcu-service"

do_install() {
    install -Dm 0644 ${S}/sshd.pam ${D}${sysconfdir}/pam.d/sshd
    install -Dm 0600 ${S}/pam_radius_auth.conf ${D}${sysconfdir}/pam_radius_auth.conf
    install -Dm 0600 ${S}/tacplus.conf ${D}${sysconfdir}/tacplus.conf
    install -Dm 0600 ${S}/nslcd.conf ${D}${sysconfdir}/nslcd.conf
}

CONFFILES:${PN} = "${sysconfdir}/pam_radius_auth.conf ${sysconfdir}/tacplus.conf ${sysconfdir}/nslcd.conf"

# nss-pam-ldapd comes from meta-networking. pam_radius and pam_tacplus are
# not in the public layers; they are packaged in the company layer.
RDEPENDS:${PN} = "libpam nss-pam-ldapd"
RRECOMMENDS:${PN} = "pam-radius pam-tacplus"
