FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

# meta-freescale merges fragments listed in DELTA_KERNEL_DEFCONFIG on top
# of the BSP defconfig.
SRC_URI += "file://tcu.cfg"
DELTA_KERNEL_DEFCONFIG:append = " tcu.cfg"
