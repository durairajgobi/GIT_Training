# Built against the OP-TEE TA dev kit, e.g. from Yocto:
#   make TA_DEV_KIT_DIR=<optee-os>/export-ta_arm64 CROSS_COMPILE=aarch64-poky-linux-
BINARY = 8d5c3a0e-6f1b-4c2e-9a47-2b1d0c9e7f31
-include $(TA_DEV_KIT_DIR)/mk/ta_dev_kit.mk

ifeq ($(wildcard $(TA_DEV_KIT_DIR)/mk/ta_dev_kit.mk), )
clean:
	@echo 'Note: $$(TA_DEV_KIT_DIR)/mk/ta_dev_kit.mk not found, cannot clean TA'
endif
