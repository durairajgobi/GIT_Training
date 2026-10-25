FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI += "file://main.conf"

# HFP HF role (wideband speech via mSBC) is served by the hands-free
# backend; the TCU needs no PAN or HID profiles.
PACKAGECONFIG:remove = "network-profiles hid-profiles"

do_install:append() {
    install -Dm 0644 ${WORKDIR}/main.conf ${D}${sysconfdir}/bluetooth/main.conf
}
