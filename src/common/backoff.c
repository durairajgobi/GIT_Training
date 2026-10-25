// SPDX-License-Identifier: MIT
#include "backoff.h"

static uint32_t xorshift32(uint32_t *s)
{
	uint32_t x = *s;

	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*s = x;
	return x;
}

void backoff_init(backoff_t *b, uint32_t base_ms, uint32_t max_ms,
		  uint32_t jitter_pct, uint32_t seed)
{
	b->base_ms = base_ms ? base_ms : 1;
	b->max_ms = max_ms < b->base_ms ? b->base_ms : max_ms;
	b->jitter_pct = jitter_pct > 100 ? 100 : jitter_pct;
	b->attempt = 0;
	b->rng = seed ? seed : 0x9e3779b9u;
}

void backoff_reset(backoff_t *b)
{
	b->attempt = 0;
}

uint32_t backoff_next(backoff_t *b)
{
	uint64_t delay = b->base_ms;
	uint32_t span;

	/* Shift one step at a time so a large attempt count cannot overflow. */
	for (uint32_t i = 0; i < b->attempt && delay < b->max_ms; i++)
		delay <<= 1;
	if (delay > b->max_ms)
		delay = b->max_ms;
	if (b->attempt < UINT32_MAX)
		b->attempt++;

	span = (uint32_t)(delay * b->jitter_pct / 100);
	if (span) {
		uint32_t r = xorshift32(&b->rng) % (2 * span + 1);

		delay = delay - span + r;
	}
	return delay ? (uint32_t)delay : 1;
}
