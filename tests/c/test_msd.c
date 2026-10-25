// SPDX-License-Identifier: MIT
#include "check.h"
#include "msd.h"

static struct msd sample(void)
{
	struct msd m = {
		.message_id = 3,
		.automatic_activation = true,
		.position_trusted = true,
		.vehicle_type = MSD_VEH_HEAVY_DUTY_N3,
		.vin = "WDB9634031L123456",
		.propulsion = MSD_FUEL_DIESEL,
		.timestamp = 1761400000u,
		.latitude_mas = 186624000,  /* 51.84 deg N */
		.longitude_mas = -324000000,/* edge of range */
		.direction = 45,
		.n1_lat_delta = -512, .n1_lon_delta = 511,
		.n2_lat_delta = 7, .n2_lon_delta = -7,
		.has_passengers = true, .passengers = 2,
	};
	return m;
}

int main(void)
{
	uint8_t buf[MSD_MAX_BYTES];
	struct msd in = sample(), out;
	int len = msd_encode(&in, buf, sizeof(buf));

	CHECK(len > 0 && len <= MSD_MAX_BYTES);
	CHECK_EQ(msd_decode(buf, (size_t)len, &out), 0);
	CHECK_EQ(out.message_id, 3);
	CHECK(out.automatic_activation && out.position_trusted && !out.test_call);
	CHECK_EQ(out.vehicle_type, MSD_VEH_HEAVY_DUTY_N3);
	CHECK_STR(out.vin, in.vin);
	CHECK_EQ(out.timestamp, in.timestamp);
	CHECK_EQ(out.latitude_mas, in.latitude_mas);
	CHECK_EQ(out.longitude_mas, in.longitude_mas);
	CHECK_EQ(out.direction, 45);
	CHECK_EQ(out.n1_lat_delta, -512);
	CHECK_EQ(out.n1_lon_delta, 511);
	CHECK_EQ(out.n2_lat_delta, 7);
	CHECK_EQ(out.n2_lon_delta, -7);
	CHECK_EQ(out.passengers, 2);

	/* Without passengers the message is one byte shorter or equal. */
	in.has_passengers = false;
	CHECK(msd_encode(&in, buf, sizeof(buf)) <= len);

	/* Invalid inputs. */
	in = sample();
	in.vin[3] = 'I';
	CHECK_EQ(msd_encode(&in, buf, sizeof(buf)), -1);
	in = sample();
	in.latitude_mas = 400000000;
	CHECK_EQ(msd_encode(&in, buf, sizeof(buf)), -1);
	in = sample();
	in.direction = 200;
	CHECK_EQ(msd_encode(&in, buf, sizeof(buf)), -1);
	in = sample();
	CHECK_EQ(msd_encode(&in, buf, 10), -1);
	CHECK_EQ(msd_decode(buf, 5, &out), -1);
	CHECK(!msd_vin_valid("SHORT"));
	CHECK_DONE();
}
