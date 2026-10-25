# OP-TEE OS platform flavours for both product generations.
# i.MX6 (legacy unit): TrustZone port on imx-mx6qsabresd with secure DDR
# carved out at the top of the 1 GiB bank (board-specific memory map).
EXTRA_OEMAKE:append:mx6-nxp-bsp = " CFG_DDR_SIZE=0x40000000 CFG_TZDRAM_SIZE=0x01e00000 \
    CFG_NS_ENTRY_ADDR=0x12000000 CFG_PSCI_ARM32=y CFG_BOOT_SECONDARY_REQUEST=y"

# Both: hardware unique key from CAAM for secure storage, RPMB anti-rollback.
EXTRA_OEMAKE:append = " CFG_RPMB_FS=y CFG_RPMB_WRITE_KEY=n CFG_CORE_HUK_SUBKEY_COMPAT=n \
    CFG_NXP_CAAM=y CFG_CRYPTO_DRIVER=y"
