/* SPDX-License-Identifier: MIT */
#ifndef TCU_MSD_H
#define TCU_MSD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Minimum Set of Data (EN 15722) carried in-band during an eCall.
 *
 * The encoder packs each field as an ASN.1 PER constrained integer
 * (value - lower bound in the minimum number of bits), using the value
 * ranges from EN 15722. It covers the mandatory fields plus the optional
 * passenger count; optional additional data (OID-tagged) is not encoded.
 * A conformance-tested ASN.1 codec must replace it before homologation.
 */

#define MSD_MAX_BYTES 140 /* EN 15722 upper bound */
#define MSD_VIN_LEN 17

enum msd_vehicle_type {
	MSD_VEH_PASSENGER_M1 = 1,
	MSD_VEH_BUS_M2 = 2,
	MSD_VEH_BUS_M3 = 3,
	MSD_VEH_LIGHT_COMMERCIAL_N1 = 4,
	MSD_VEH_HEAVY_DUTY_N2 = 5,
	MSD_VEH_HEAVY_DUTY_N3 = 6,
};

/* vehiclePropulsionStorageType bit flags */
#define MSD_FUEL_GASOLINE (1u << 0)
#define MSD_FUEL_DIESEL (1u << 1)
#define MSD_FUEL_CNG (1u << 2)
#define MSD_FUEL_LPG (1u << 3)
#define MSD_FUEL_ELECTRIC (1u << 4)
#define MSD_FUEL_HYDROGEN (1u << 5)

struct msd {
	uint8_t message_id;       /* incremented on every MSD resend */
	bool automatic_activation;
	bool test_call;
	bool position_trusted;
	uint8_t vehicle_type;     /* enum msd_vehicle_type */
	char vin[MSD_VIN_LEN + 1];
	uint8_t propulsion;       /* MSD_FUEL_* */
	uint32_t timestamp;       /* seconds since 1970-01-01 UTC */
	int32_t latitude_mas;     /* milliarcseconds, -324000000..324000000 */
	int32_t longitude_mas;    /* milliarcseconds, -648000000..648000000 */
	uint8_t direction;        /* 2-degree steps, 0..179; 255 = unknown */
	/* Previous two positions as deltas in 100 mas units, -512..511. */
	int16_t n1_lat_delta, n1_lon_delta;
	int16_t n2_lat_delta, n2_lon_delta;
	bool has_passengers;
	uint8_t passengers;
};

/* Returns encoded length in bytes, or -1 on invalid input / short buffer. */
int msd_encode(const struct msd *m, uint8_t *buf, size_t len);

/* Returns 0 on success, -1 on malformed input. */
int msd_decode(const uint8_t *buf, size_t len, struct msd *m);

/* Validates characters allowed in a VIN (ISO 3779: no I, O, Q). */
bool msd_vin_valid(const char *vin);

#endif
