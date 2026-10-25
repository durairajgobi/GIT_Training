/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef USER_TA_HEADER_DEFINES_H
#define USER_TA_HEADER_DEFINES_H

#include "tcu_keystore_ta.h"

#define TA_UUID TA_TCU_KEYSTORE_UUID
#define TA_FLAGS (TA_FLAG_EXEC_DDR | TA_FLAG_SINGLE_INSTANCE | TA_FLAG_MULTI_SESSION)
#define TA_STACK_SIZE (4 * 1024)
#define TA_DATA_SIZE (32 * 1024)

#define TA_CURRENT_TA_EXT_PROPERTIES \
	{ "gp.ta.description", USER_TA_PROP_TYPE_STRING, "TCU device identity keystore" }, \
	{ "gp.ta.version", USER_TA_PROP_TYPE_U32, &(const uint32_t){ 0x0100 } }

#endif
