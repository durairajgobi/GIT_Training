/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef TCU_KEYSTORE_TA_H
#define TCU_KEYSTORE_TA_H

/* 8d5c3a0e-6f1b-4c2e-9a47-2b1d0c9e7f31 */
#define TA_TCU_KEYSTORE_UUID \
	{ 0x8d5c3a0e, 0x6f1b, 0x4c2e, \
	  { 0x9a, 0x47, 0x2b, 0x1d, 0x0c, 0x9e, 0x7f, 0x31 } }

/*
 * Commands. Key ids are short ASCII names ("device", "v2x", ...).
 *
 * GENERATE  params[0] memref IN  key id
 *           -> TEE_ERROR_ACCESS_CONFLICT if the key already exists:
 *              a device identity is created once at end-of-line.
 * PUBKEY    params[0] memref IN  key id
 *           params[1] memref OUT 65-byte uncompressed P-256 point
 * SIGN      params[0] memref IN  key id
 *           params[1] memref IN  32-byte SHA-256 digest
 *           params[2] memref OUT 64-byte raw r||s signature
 */
#define TA_TCU_KS_CMD_GENERATE 0
#define TA_TCU_KS_CMD_PUBKEY   1
#define TA_TCU_KS_CMD_SIGN     2

#define TA_TCU_KS_MAX_ID 32

#endif
