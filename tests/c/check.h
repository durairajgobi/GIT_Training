/* SPDX-License-Identifier: MIT */
/* Minimal assertion helpers for host unit tests; exit code = failures. */
#ifndef TCU_CHECK_H
#define TCU_CHECK_H

#include <stdio.h>
#include <string.h>

static int check_failures;

#define CHECK(cond) do { \
	if (!(cond)) { \
		fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
		check_failures++; \
	} \
} while (0)

#define CHECK_EQ(a, b) do { \
	long long _a = (long long)(a), _b = (long long)(b); \
	if (_a != _b) { \
		fprintf(stderr, "%s:%d: %s == %s failed (%lld != %lld)\n", \
			__FILE__, __LINE__, #a, #b, _a, _b); \
		check_failures++; \
	} \
} while (0)

#define CHECK_STR(a, b) do { \
	if (strcmp((a), (b)) != 0) { \
		fprintf(stderr, "%s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); \
		check_failures++; \
	} \
} while (0)

#define CHECK_DONE() do { \
	if (check_failures) \
		fprintf(stderr, "%d check(s) failed\n", check_failures); \
	return check_failures ? 1 : 0; \
} while (0)

#endif
