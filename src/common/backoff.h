/* SPDX-License-Identifier: MIT */
#ifndef TCU_BACKOFF_H
#define TCU_BACKOFF_H

#include <stdint.h>

/*
 * Capped exponential backoff with symmetric jitter.
 *
 *   delay(n) = min(max_ms, base_ms * 2^n) +/- jitter_pct%
 *
 * Jitter keeps a fleet of units from retrying in lock-step after a cell
 * outage. The PRNG is a local xorshift so tests are deterministic.
 */
typedef struct {
	uint32_t base_ms;
	uint32_t max_ms;
	uint32_t jitter_pct; /* 0..100 */
	uint32_t attempt;
	uint32_t rng;
} backoff_t;

void backoff_init(backoff_t *b, uint32_t base_ms, uint32_t max_ms,
		  uint32_t jitter_pct, uint32_t seed);
uint32_t backoff_next(backoff_t *b);
void backoff_reset(backoff_t *b);

#endif
