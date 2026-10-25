// SPDX-License-Identifier: BSD-2-Clause
/*
 * tcu_keystore - device identity keys in OP-TEE secure storage.
 *
 * The private key is generated inside the TEE, restricted to signing and
 * never leaves it. Normal world only ever sees the public key and
 * signatures (for the mTLS client certificate and backend challenges).
 * Storage is TEE_STORAGE_PRIVATE: REE FS encrypted with a key derived from
 * the hardware unique key (OTPMK via CAAM on i.MX).
 */
#include <string.h>
#include <tee_internal_api.h>

#include "tcu_keystore_ta.h"

#ifndef TEE_ALG_ECDSA_SHA256
#define TEE_ALG_ECDSA_SHA256 TEE_ALG_ECDSA_P256
#endif

TEE_Result TA_CreateEntryPoint(void) { return TEE_SUCCESS; }
void TA_DestroyEntryPoint(void) {}

TEE_Result TA_OpenSessionEntryPoint(uint32_t types, TEE_Param params[4], void **sess)
{
	(void)params;
	(void)sess;
	if (types != TEE_PARAM_TYPES(TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE,
				     TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE))
		return TEE_ERROR_BAD_PARAMETERS;
	return TEE_SUCCESS;
}

void TA_CloseSessionEntryPoint(void *sess) { (void)sess; }

static TEE_Result check_id(const TEE_Param *p)
{
	if (!p->memref.buffer || p->memref.size == 0 || p->memref.size > TA_TCU_KS_MAX_ID)
		return TEE_ERROR_BAD_PARAMETERS;
	return TEE_SUCCESS;
}

static TEE_Result open_key(const TEE_Param *id, TEE_ObjectHandle *obj)
{
	return TEE_OpenPersistentObject(TEE_STORAGE_PRIVATE, id->memref.buffer, id->memref.size,
					TEE_DATA_FLAG_ACCESS_READ, obj);
}

static TEE_Result cmd_generate(uint32_t types, TEE_Param p[4])
{
	TEE_ObjectHandle key = TEE_HANDLE_NULL, pobj = TEE_HANDLE_NULL;
	TEE_Attribute curve;
	TEE_Result res;

	if (types != TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT, TEE_PARAM_TYPE_NONE,
				     TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE))
		return TEE_ERROR_BAD_PARAMETERS;
	if ((res = check_id(&p[0])) != TEE_SUCCESS)
		return res;

	res = TEE_AllocateTransientObject(TEE_TYPE_ECDSA_KEYPAIR, 256, &key);
	if (res != TEE_SUCCESS)
		return res;
	TEE_InitValueAttribute(&curve, TEE_ATTR_ECC_CURVE, TEE_ECC_CURVE_NIST_P256, 0);
	res = TEE_GenerateKey(key, 256, &curve, 1);
	if (res != TEE_SUCCESS)
		goto out;
	/* Drops TEE_USAGE_EXTRACTABLE: the private value can never be read. */
	res = TEE_RestrictObjectUsage1(key, TEE_USAGE_SIGN | TEE_USAGE_VERIFY);
	if (res != TEE_SUCCESS)
		goto out;
	/* No TEE_DATA_FLAG_OVERWRITE: an existing identity is never replaced. */
	res = TEE_CreatePersistentObject(TEE_STORAGE_PRIVATE, p[0].memref.buffer,
					 p[0].memref.size, TEE_DATA_FLAG_ACCESS_READ, key,
					 NULL, 0, &pobj);
	if (res == TEE_SUCCESS)
		TEE_CloseObject(pobj);
out:
	TEE_FreeTransientObject(key);
	return res;
}

static TEE_Result cmd_pubkey(uint32_t types, TEE_Param p[4])
{
	TEE_ObjectHandle key;
	uint8_t *out = p[1].memref.buffer;
	uint32_t xlen = 32, ylen = 32;
	TEE_Result res;

	if (types != TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT, TEE_PARAM_TYPE_MEMREF_OUTPUT,
				     TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE))
		return TEE_ERROR_BAD_PARAMETERS;
	if ((res = check_id(&p[0])) != TEE_SUCCESS)
		return res;
	if (p[1].memref.size < 65) {
		p[1].memref.size = 65;
		return TEE_ERROR_SHORT_BUFFER;
	}
	if ((res = open_key(&p[0], &key)) != TEE_SUCCESS)
		return res;

	out[0] = 0x04; /* SEC1 uncompressed point */
	res = TEE_GetObjectBufferAttribute(key, TEE_ATTR_ECC_PUBLIC_VALUE_X, out + 1, &xlen);
	if (res == TEE_SUCCESS)
		res = TEE_GetObjectBufferAttribute(key, TEE_ATTR_ECC_PUBLIC_VALUE_Y, out + 33, &ylen);
	if (res == TEE_SUCCESS && (xlen != 32 || ylen != 32))
		res = TEE_ERROR_GENERIC; /* leading-zero trimmed coordinate; not expected */
	if (res == TEE_SUCCESS)
		p[1].memref.size = 65;
	TEE_CloseObject(key);
	return res;
}

static TEE_Result cmd_sign(uint32_t types, TEE_Param p[4])
{
	TEE_OperationHandle op = TEE_HANDLE_NULL;
	TEE_ObjectHandle key;
	uint32_t siglen;
	TEE_Result res;

	if (types != TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT, TEE_PARAM_TYPE_MEMREF_INPUT,
				     TEE_PARAM_TYPE_MEMREF_OUTPUT, TEE_PARAM_TYPE_NONE))
		return TEE_ERROR_BAD_PARAMETERS;
	if ((res = check_id(&p[0])) != TEE_SUCCESS)
		return res;
	if (p[1].memref.size != 32)
		return TEE_ERROR_BAD_PARAMETERS;
	if (p[2].memref.size < 64) {
		p[2].memref.size = 64;
		return TEE_ERROR_SHORT_BUFFER;
	}
	if ((res = open_key(&p[0], &key)) != TEE_SUCCESS)
		return res;

	res = TEE_AllocateOperation(&op, TEE_ALG_ECDSA_SHA256, TEE_MODE_SIGN, 256);
	if (res == TEE_SUCCESS)
		res = TEE_SetOperationKey(op, key);
	if (res == TEE_SUCCESS) {
		siglen = p[2].memref.size;
		res = TEE_AsymmetricSignDigest(op, NULL, 0, p[1].memref.buffer, 32,
					       p[2].memref.buffer, &siglen);
		if (res == TEE_SUCCESS)
			p[2].memref.size = siglen;
	}
	if (op != TEE_HANDLE_NULL)
		TEE_FreeOperation(op);
	TEE_CloseObject(key);
	return res;
}

TEE_Result TA_InvokeCommandEntryPoint(void *sess, uint32_t cmd, uint32_t types, TEE_Param p[4])
{
	(void)sess;
	switch (cmd) {
	case TA_TCU_KS_CMD_GENERATE: return cmd_generate(types, p);
	case TA_TCU_KS_CMD_PUBKEY: return cmd_pubkey(types, p);
	case TA_TCU_KS_CMD_SIGN: return cmd_sign(types, p);
	default: return TEE_ERROR_NOT_SUPPORTED;
	}
}
