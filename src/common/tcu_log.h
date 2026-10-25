/* SPDX-License-Identifier: MIT */
#ifndef TCU_LOG_H
#define TCU_LOG_H

#include <stdio.h>
#include <syslog.h>

/*
 * Daemons log to syslog (journald on target). Unit tests and host builds
 * define TCU_LOG_STDERR to get plain stderr output.
 */
#ifdef TCU_LOG_STDERR
#define TCU_LOG(prio, fmt, ...) \
	fprintf(stderr, "[%d] " fmt "\n", prio, ##__VA_ARGS__)
#else
#define TCU_LOG(prio, fmt, ...) syslog(prio, fmt, ##__VA_ARGS__)
#endif

#define LOG_E(fmt, ...) TCU_LOG(LOG_ERR, fmt, ##__VA_ARGS__)
#define LOG_W(fmt, ...) TCU_LOG(LOG_WARNING, fmt, ##__VA_ARGS__)
#define LOG_I(fmt, ...) TCU_LOG(LOG_INFO, fmt, ##__VA_ARGS__)
#define LOG_D(fmt, ...) TCU_LOG(LOG_DEBUG, fmt, ##__VA_ARGS__)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#endif
