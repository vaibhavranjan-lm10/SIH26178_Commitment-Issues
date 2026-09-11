/*
 * Watchdog supervisor.  The hardware watchdog (IWDG) is fed only while
 * every registered activity has checked in inside its own deadline —
 * a hung bus sweep, TDMA loop, PPS/GNSS task or RPC link each stops the
 * feed and the SoC resets.  Pure; time injected.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef HN_SUPERVISOR_H_
#define HN_SUPERVISOR_H_

#include <stdbool.h>
#include <stdint.h>

#define SUP_MAX_CHANNELS 8

struct sup_channel {
	const char *name;
	uint32_t deadline_ms; /* max silence tolerated */
	uint32_t last_ms;
	bool armed;           /* channel participates once it has checked in the first time */
	bool required_at_boot;/* must check in within deadline of init, else stale */
};

struct supervisor {
	struct sup_channel ch[SUP_MAX_CHANNELS];
	uint8_t n;
	uint32_t t_init;
	uint32_t feeds, refusals;
};

void sup_init(struct supervisor *s, uint32_t now_ms);
/* Returns channel id or -ENOSPC. */
int sup_register(struct supervisor *s, const char *name, uint32_t deadline_ms, bool required_at_boot);
void sup_checkin(struct supervisor *s, int ch, uint32_t now_ms);
/* Bitmask of channels currently overdue. */
uint32_t sup_stale_mask(const struct supervisor *s, uint32_t now_ms);
/* True (and counts a feed) only when nothing is stale. */
bool sup_should_feed(struct supervisor *s, uint32_t now_ms);

#endif /* HN_SUPERVISOR_H_ */
