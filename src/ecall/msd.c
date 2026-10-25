// SPDX-License-Identifier: MIT
#include "msd.h"

#include <string.h>

#define MSD_FORMAT_VERSION 2

static const char vin_alphabet[] = "0123456789ABCDEFGHJKLMNPRSTUVWXYZ";

struct bitbuf {
	uint8_t *buf;
	size_t len;   /* bytes */
	size_t pos;   /* bits */
	bool err;
};

static void put_bits(struct bitbuf *b, uint32_t v, unsigned int n)
{
	for (int i = (int)n - 1; i >= 0; i--) {
		size_t byte = b->pos >> 3;

		if (byte >= b->len) {
			b->err = true;
			return;
		}
		if ((v >> i) & 1u)
			b->buf[byte] |= (uint8_t)(0x80u >> (b->pos & 7));
		b->pos++;
	}
}

static uint32_t get_bits(struct bitbuf *b, unsigned int n)
{
	uint32_t v = 0;

	for (unsigned int i = 0; i < n; i++) {
		size_t byte = b->pos >> 3;

		if (byte >= b->len) {
			b->err = true;
			return 0;
		}
		v = (v << 1) | ((b->buf[byte] >> (7 - (b->pos & 7))) & 1u);
		b->pos++;
	}
	return v;
}

/* PER constrained whole number: (v - lb) in ceil(log2(ub - lb + 1)) bits. */
static unsigned int range_bits(int64_t lb, int64_t ub)
{
	uint64_t range = (uint64_t)(ub - lb);
	unsigned int n = 0;

	while (range) {
		n++;
		range >>= 1;
	}
	return n;
}

static void put_int(struct bitbuf *b, int64_t v, int64_t lb, int64_t ub)
{
	if (v < lb || v > ub) {
		b->err = true;
		return;
	}
	put_bits(b, (uint32_t)(v - lb), range_bits(lb, ub));
}

static int64_t get_int(struct bitbuf *b, int64_t lb, int64_t ub)
{
	int64_t v = (int64_t)get_bits(b, range_bits(lb, ub)) + lb;

	if (v > ub)
		b->err = true;
	return v;
}

bool msd_vin_valid(const char *vin)
{
	if (!vin || strlen(vin) != MSD_VIN_LEN)
		return false;
	for (size_t i = 0; i < MSD_VIN_LEN; i++)
		if (!strchr(vin_alphabet, vin[i]) || vin[i] == '\0')
			return false;
	return true;
}

#define LAT_MIN (-324000000LL)
#define LAT_MAX 324000000LL
#define LON_MIN (-648000000LL)
#define LON_MAX 648000000LL

int msd_encode(const struct msd *m, uint8_t *buf, size_t len)
{
	struct bitbuf b = { .buf = buf, .len = len };

	if (!m || !buf || !msd_vin_valid(m->vin))
		return -1;
	if (m->direction > 179 && m->direction != 255)
		return -1;
	memset(buf, 0, len);

	put_bits(&b, MSD_FORMAT_VERSION, 8);
	put_bits(&b, m->message_id, 8);
	put_bits(&b, m->automatic_activation, 1);
	put_bits(&b, m->test_call, 1);
	put_bits(&b, m->position_trusted, 1);
	put_int(&b, m->vehicle_type, 1, 15);
	for (size_t i = 0; i < MSD_VIN_LEN; i++)
		put_bits(&b, (uint32_t)(strchr(vin_alphabet, m->vin[i]) - vin_alphabet), 6);
	put_bits(&b, m->propulsion, 8);
	put_bits(&b, m->timestamp, 32);
	put_int(&b, m->latitude_mas, LAT_MIN, LAT_MAX);
	put_int(&b, m->longitude_mas, LON_MIN, LON_MAX);
	put_bits(&b, m->direction, 8);
	put_int(&b, m->n1_lat_delta, -512, 511);
	put_int(&b, m->n1_lon_delta, -512, 511);
	put_int(&b, m->n2_lat_delta, -512, 511);
	put_int(&b, m->n2_lon_delta, -512, 511);
	put_bits(&b, m->has_passengers, 1);
	if (m->has_passengers)
		put_bits(&b, m->passengers, 8);

	if (b.err)
		return -1;
	return (int)((b.pos + 7) / 8);
}

int msd_decode(const uint8_t *buf, size_t len, struct msd *m)
{
	struct bitbuf b = { .buf = (uint8_t *)buf, .len = len };

	if (!buf || !m)
		return -1;
	memset(m, 0, sizeof(*m));
	if (get_bits(&b, 8) != MSD_FORMAT_VERSION)
		return -1;
	m->message_id = (uint8_t)get_bits(&b, 8);
	m->automatic_activation = get_bits(&b, 1);
	m->test_call = get_bits(&b, 1);
	m->position_trusted = get_bits(&b, 1);
	m->vehicle_type = (uint8_t)get_int(&b, 1, 15);
	for (size_t i = 0; i < MSD_VIN_LEN; i++) {
		uint32_t idx = get_bits(&b, 6);

		if (idx >= sizeof(vin_alphabet) - 1)
			return -1;
		m->vin[i] = vin_alphabet[idx];
	}
	m->propulsion = (uint8_t)get_bits(&b, 8);
	m->timestamp = get_bits(&b, 32);
	m->latitude_mas = (int32_t)get_int(&b, LAT_MIN, LAT_MAX);
	m->longitude_mas = (int32_t)get_int(&b, LON_MIN, LON_MAX);
	m->direction = (uint8_t)get_bits(&b, 8);
	m->n1_lat_delta = (int16_t)get_int(&b, -512, 511);
	m->n1_lon_delta = (int16_t)get_int(&b, -512, 511);
	m->n2_lat_delta = (int16_t)get_int(&b, -512, 511);
	m->n2_lon_delta = (int16_t)get_int(&b, -512, 511);
	m->has_passengers = get_bits(&b, 1);
	if (m->has_passengers)
		m->passengers = (uint8_t)get_bits(&b, 8);
	return b.err ? -1 : 0;
}
