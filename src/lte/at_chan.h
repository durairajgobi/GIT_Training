/* SPDX-License-Identifier: MIT */
#ifndef TCU_AT_CHAN_H
#define TCU_AT_CHAN_H

#include <stdbool.h>

/*
 * Several users share one AT port: lte_sm, and the daemon's own status
 * polling / RAT steering. The modem answers commands strictly in order,
 * so a FIFO of "who sent it" is enough to route each final result code
 * (OK / ERROR) back to its sender.
 */

typedef enum { AT_OWNER_NONE = -1, AT_OWNER_SM = 0, AT_OWNER_AUX = 1 } at_owner_t;

#define AT_CHAN_DEPTH 16

struct at_chan {
	unsigned char q[AT_CHAN_DEPTH];
	unsigned int head, count;
};

void at_chan_reset(struct at_chan *c);
/* Record a command just written. False if the queue is full. */
bool at_chan_push(struct at_chan *c, at_owner_t who);
/* Final result received: who sent the oldest outstanding command. */
at_owner_t at_chan_pop(struct at_chan *c);
unsigned int at_chan_pending(const struct at_chan *c, at_owner_t who);

#endif
