/*
 * ep_crc.h - CRCs used by the protocols in this library.
 *
 *   crc8_smbus    poly 0x07, init 0x00          SMBus PEC
 *   crc16_modbus  poly 0x8005 refl, init 0xFFFF Modbus RTU
 *   crc16_ccitt   poly 0x1021, init 0xFFFF      HDLC-like serial framing
 *   crc32_ieee    poly 0x04C11DB7 refl          Ethernet FCS
 *
 * All functions accept a running value so data can be fed in pieces:
 *   crc = ep_crc16_modbus(EP_CRC16_MODBUS_INIT, a, n); crc = ep_crc16_modbus(crc, b, m);
 * For CRC-32 pass the result of the previous call; the final XOR is
 * applied internally on every call and undone on entry.
 */
#ifndef EP_CRC_H
#define EP_CRC_H

#include "ep_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EP_CRC8_SMBUS_INIT   0x00u
#define EP_CRC16_MODBUS_INIT 0xFFFFu
#define EP_CRC16_CCITT_INIT  0xFFFFu
#define EP_CRC32_INIT        0x00000000u

uint8_t ep_crc8_smbus(uint8_t crc, const uint8_t *data, size_t len);
uint16_t ep_crc16_modbus(uint16_t crc, const uint8_t *data, size_t len);
uint16_t ep_crc16_ccitt(uint16_t crc, const uint8_t *data, size_t len);
uint32_t ep_crc32_ieee(uint32_t crc, const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* EP_CRC_H */
