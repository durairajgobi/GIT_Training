// SPDX-License-Identifier: MIT
#include "at_chan.h"

void at_chan_reset(struct at_chan *c)
{
	c->head = 0;
	c->count = 0;
}

bool at_chan_push(struct at_chan *c, at_owner_t who)
{
	if (c->count >= AT_CHAN_DEPTH)
		return false;
	c->q[(c->head + c->count) % AT_CHAN_DEPTH] = (unsigned char)who;
	c->count++;
	return true;
}

at_owner_t at_chan_pop(struct at_chan *c)
{
	at_owner_t who;

	if (!c->count)
		return AT_OWNER_NONE;
	who = (at_owner_t)c->q[c->head];
	c->head = (c->head + 1) % AT_CHAN_DEPTH;
	c->count--;
	return who;
}

unsigned int at_chan_pending(const struct at_chan *c, at_owner_t who)
{
	unsigned int n = 0;

	for (unsigned int i = 0; i < c->count; i++)
		if (c->q[(c->head + i) % AT_CHAN_DEPTH] == (unsigned char)who)
			n++;
	return n;
}
