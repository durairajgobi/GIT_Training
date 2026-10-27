// SPDX-License-Identifier: GPL-2.0
/*
 * bl_ctl.c - User-space control tool for the PWM LCD backlight driver
 *
 * Usage:
 *   bl_ctl enable
 *   bl_ctl disable
 *   bl_ctl status
 *   bl_ctl brightness <0..max>
 *   bl_ctl config <period_ns> <duty_ns> [normal|inversed]
 */
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "pwm_lcd_bl.h"

#define DEV_PATH "/dev/" LCD_BL_DEV_NAME

static void usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s <command> [args]\n"
		"  enable                                   turn the backlight on\n"
		"  disable                                  turn the backlight off\n"
		"  status                                   print current configuration\n"
		"  brightness <level>                       set brightness (0..max)\n"
		"  config <period_ns> <duty_ns> [polarity]  set PWM config (normal|inversed)\n",
		prog);
}

static int parse_u64(const char *s, uint64_t *out)
{
	char *end;

	errno = 0;
	*out = strtoull(s, &end, 0);
	return (errno || *end || end == s) ? -1 : 0;
}

static int print_status(int fd)
{
	struct lcd_bl_config cfg;

	if (ioctl(fd, LCD_BL_IOC_GET_CONFIG, &cfg) < 0) {
		perror("LCD_BL_IOC_GET_CONFIG");
		return -1;
	}
	printf("enabled        : %u\n", cfg.enabled);
	printf("period_ns      : %" PRIu64 "\n", (uint64_t)cfg.period_ns);
	printf("duty_ns        : %" PRIu64 "\n", (uint64_t)cfg.duty_ns);
	printf("polarity       : %s\n",
	       cfg.polarity == LCD_BL_POLARITY_INVERSED ? "inversed" : "normal");
	printf("brightness     : %u / %u\n", cfg.brightness, cfg.max_brightness);
	return 0;
}

int main(int argc, char **argv)
{
	const char *cmd;
	int fd, ret = 0;

	if (argc < 2) {
		usage(argv[0]);
		return EXIT_FAILURE;
	}
	cmd = argv[1];

	fd = open(DEV_PATH, O_RDWR);
	if (fd < 0) {
		perror("open " DEV_PATH);
		return EXIT_FAILURE;
	}

	if (!strcmp(cmd, "enable")) {
		ret = ioctl(fd, LCD_BL_IOC_ENABLE);
		if (ret < 0)
			perror("LCD_BL_IOC_ENABLE");
	} else if (!strcmp(cmd, "disable")) {
		ret = ioctl(fd, LCD_BL_IOC_DISABLE);
		if (ret < 0)
			perror("LCD_BL_IOC_DISABLE");
	} else if (!strcmp(cmd, "status")) {
		ret = print_status(fd);
	} else if (!strcmp(cmd, "brightness") && argc == 3) {
		uint64_t val;
		__u32 level;

		if (parse_u64(argv[2], &val) || val > UINT32_MAX) {
			fprintf(stderr, "invalid brightness '%s'\n", argv[2]);
			ret = -1;
		} else {
			level = (__u32)val;
			ret = ioctl(fd, LCD_BL_IOC_SET_BRIGHTNESS, &level);
			if (ret < 0)
				perror("LCD_BL_IOC_SET_BRIGHTNESS");
		}
	} else if (!strcmp(cmd, "config") && (argc == 4 || argc == 5)) {
		struct lcd_bl_config cfg = { 0 };
		uint64_t period, duty;

		if (parse_u64(argv[2], &period) || parse_u64(argv[3], &duty)) {
			fprintf(stderr, "invalid period/duty\n");
			ret = -1;
		} else if (argc == 5 && strcmp(argv[4], "normal") &&
			   strcmp(argv[4], "inversed")) {
			fprintf(stderr, "polarity must be 'normal' or 'inversed'\n");
			ret = -1;
		} else {
			cfg.period_ns = period;
			cfg.duty_ns = duty;
			cfg.polarity = (argc == 5 && !strcmp(argv[4], "inversed")) ?
				       LCD_BL_POLARITY_INVERSED : LCD_BL_POLARITY_NORMAL;
			ret = ioctl(fd, LCD_BL_IOC_SET_CONFIG, &cfg);
			if (ret < 0)
				perror("LCD_BL_IOC_SET_CONFIG");
		}
	} else {
		usage(argv[0]);
		ret = -1;
	}

	close(fd);
	return ret < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
