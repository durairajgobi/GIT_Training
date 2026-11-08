/* SPDX-License-Identifier: MIT */
/*
 * pmic_ltc3562.c - Bring up LTC3562 rails through devhal.
 *
 * Shows how a portable chip driver (../ltc3562) plugs into the middleware:
 * the driver only needs an I2C write callback, and hal_i2c_write_cb with a
 * hal_i2c_t handle provides it, along with retries and logging.
 *
 *   pmic_ltc3562 <bus> R600B=1800 R400B=3300 ...   (mode: pulse-skip)
 *   pmic_ltc3562 <bus> off
 */
#include "devhal/hal_i2c.h"
#include "ltc3562.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_rail(const char *s, ltc3562_reg_t *reg, uint32_t *mv)
{
	static const char *const names[] = { "R600A", "R400A", "R600B", "R400B" };
	const char *eq = strchr(s, '=');
	int i;

	if (!eq)
		return -1;
	for (i = 0; i < 4; i++) {
		if ((size_t)(eq - s) == strlen(names[i]) &&
		    !strncmp(s, names[i], (size_t)(eq - s))) {
			*reg = (ltc3562_reg_t)i;
			*mv = (uint32_t)strtoul(eq + 1, NULL, 10);
			return 0;
		}
	}
	return -1;
}

int main(int argc, char **argv)
{
	ltc3562_dev_t pmic;
	hal_i2c_t *bus;
	int i, r, rc = 0;

	if (argc < 3) {
		fprintf(stderr, "usage: %s <bus> off | <RAIL>=<mV>...\n"
			"  Type B rails (R600B, R400B) only; Type A rails "
			"need their divider, see ../ltc3562.\n", argv[0]);
		return 2;
	}

	r = hal_i2c_open(&bus, (unsigned int)strtoul(argv[1], NULL, 0));
	if (r < 0) {
		fprintf(stderr, "i2c: %s\n", hal_strerror(r));
		return 1;
	}
	/* A PMIC on a shared bus may be briefly busy; retry twice, 1 ms apart. */
	hal_i2c_set_retries(bus, 2, 1000);

	/* Puts every rail into a known (off) state. */
	r = ltc3562_init(&pmic, hal_i2c_write_cb, bus, 0);
	if (r != LTC3562_OK) {
		fprintf(stderr, "ltc3562_init failed (%d)\n", r);
		hal_i2c_close(bus);
		return 1;
	}

	if (!strcmp(argv[2], "off")) {
		hal_i2c_close(bus);
		return 0;
	}

	for (i = 2; i < argc; i++) {
		ltc3562_reg_t reg;
		uint32_t mv, actual = 0;

		if (parse_rail(argv[i], &reg, &mv) < 0) {
			fprintf(stderr, "bad rail spec: %s\n", argv[i]);
			rc = 2;
			continue;
		}
		r = ltc3562_configure(&pmic, reg, mv, LTC3562_MODE_PULSE_SKIP,
				      true, &actual);
		if (r != LTC3562_OK) {
			fprintf(stderr, "%s: error %d\n", ltc3562_reg_name(reg), r);
			rc = 1;
		} else {
			printf("%s: %u mV\n", ltc3562_reg_name(reg), actual);
		}
	}
	hal_i2c_close(bus);
	return rc;
}
