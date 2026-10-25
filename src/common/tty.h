/* SPDX-License-Identifier: MIT */
#ifndef TCU_TTY_H
#define TCU_TTY_H

#include <stddef.h>

/* Open a modem AT port in raw, non-blocking mode. Returns fd or -1. */
int tty_open_raw(const char *path, int baud);

/* Write a full AT command terminated with CR. Returns 0 or -1. */
int tty_write_cmd(int fd, const char *cmd);

/* Arm a one-shot CLOCK_MONOTONIC timerfd; ms == 0 disarms. */
int timerfd_arm_ms(int tfd, unsigned int ms);

#endif
