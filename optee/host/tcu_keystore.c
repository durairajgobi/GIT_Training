// SPDX-License-Identifier: BSD-2-Clause
/*
 * tcu-keystore - normal-world client for the keystore TA.
 *
 *   tcu-keystore generate <id>
 *   tcu-keystore pubkey   <id>            # hex SEC1 point on stdout
 *   tcu-keystore sign     <id> <sha256-hex>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tee_client_api.h>

#include "tcu_keystore_ta.h"

static int unhex(const char *s, uint8_t *out, size_t len)
{
	if (strlen(s) != 2 * len)
		return -1;
	for (size_t i = 0; i < len; i++)
		if (sscanf(s + 2 * i, "%2hhx", &out[i]) != 1)
			return -1;
	return 0;
}

static void print_hex(const uint8_t *b, size_t n)
{
	for (size_t i = 0; i < n; i++)
		printf("%02x", b[i]);
	printf("\n");
}

int main(int argc, char **argv)
{
	TEEC_Context ctx;
	TEEC_Session sess;
	TEEC_Operation op = { 0 };
	TEEC_UUID uuid = TA_TCU_KEYSTORE_UUID;
	uint8_t out[65], digest[32];
	uint32_t origin, cmd;
	TEEC_Result res;

	if (argc < 3) {
		fprintf(stderr, "usage: %s generate|pubkey|sign <id> [digest]\n", argv[0]);
		return 2;
	}
	op.params[0].tmpref.buffer = argv[2];
	op.params[0].tmpref.size = strlen(argv[2]);

	if (!strcmp(argv[1], "generate")) {
		cmd = TA_TCU_KS_CMD_GENERATE;
		op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT, TEEC_NONE, TEEC_NONE, TEEC_NONE);
	} else if (!strcmp(argv[1], "pubkey")) {
		cmd = TA_TCU_KS_CMD_PUBKEY;
		op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT, TEEC_MEMREF_TEMP_OUTPUT,
						 TEEC_NONE, TEEC_NONE);
		op.params[1].tmpref.buffer = out;
		op.params[1].tmpref.size = 65;
	} else if (!strcmp(argv[1], "sign") && argc == 4 && unhex(argv[3], digest, 32) == 0) {
		cmd = TA_TCU_KS_CMD_SIGN;
		op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT, TEEC_MEMREF_TEMP_INPUT,
						 TEEC_MEMREF_TEMP_OUTPUT, TEEC_NONE);
		op.params[1].tmpref.buffer = digest;
		op.params[1].tmpref.size = 32;
		op.params[2].tmpref.buffer = out;
		op.params[2].tmpref.size = 64;
	} else {
		fprintf(stderr, "bad arguments\n");
		return 2;
	}

	if ((res = TEEC_InitializeContext(NULL, &ctx)) != TEEC_SUCCESS ||
	    (res = TEEC_OpenSession(&ctx, &sess, &uuid, TEEC_LOGIN_PUBLIC, NULL, NULL,
				    &origin)) != TEEC_SUCCESS) {
		fprintf(stderr, "TEE unavailable: 0x%x\n", res);
		return 1;
	}
	res = TEEC_InvokeCommand(&sess, cmd, &op, &origin);
	if (res != TEEC_SUCCESS) {
		fprintf(stderr, "command failed: 0x%x (origin %u)\n", res, origin);
	} else if (cmd == TA_TCU_KS_CMD_PUBKEY) {
		print_hex(out, op.params[1].tmpref.size);
	} else if (cmd == TA_TCU_KS_CMD_SIGN) {
		print_hex(out, op.params[2].tmpref.size);
	}
	TEEC_CloseSession(&sess);
	TEEC_FinalizeContext(&ctx);
	return res == TEEC_SUCCESS ? 0 : 1;
}
