/*
 * Power sequencing for the head node's own rails (blueprint §6.4):
 * 12 V mast bus (pods, RS-485), 5 V buck (compute), 3.3 V buck (radios),
 * plus the modem.  Ordered bring-up with settle delays, and load shedding
 * by priority as battery voltage falls, with hysteresis.  Pure; time and
 * battery voltage injected.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef HN_RAILS_H_
#define HN_RAILS_H_

#include <stdbool.h>
#include <stdint.h>

#define RAILS_MAX 8

struct rail_cfg {
	const char *name;
	uint16_t settle_ms;     /* wait after enabling before the next rail */
	uint8_t shed_priority;  /* 0 = never shed; higher sheds first */
	uint16_t shed_mv;       /* shed below this battery voltage (0 = never) */
	uint16_t restore_mv;    /* restore above this (must exceed shed_mv) */
};

enum rail_state {
	RAIL_OFF = 0,
	RAIL_SETTLING,
	RAIL_ON,
	RAIL_SHED,
};

struct rails {
	const struct rail_cfg *cfg;
	uint8_t n;
	enum rail_state st[RAILS_MAX];
	bool wanted[RAILS_MAX];
	uint32_t t_change[RAILS_MAX];
	/* injected */
	int (*set)(void *ctx, uint8_t rail, bool on);
	void *ctx;
	uint32_t sheds, restores;
};

int rails_init(struct rails *r, const struct rail_cfg *cfg, uint8_t n,
	       int (*set)(void *, uint8_t, bool), void *ctx, uint32_t now_ms);
/* Request a rail on/off (bring-up order is table order, one at a time). */
void rails_request(struct rails *r, uint8_t rail, bool on);
/* Advance: sequencing, shedding, restoring. */
void rails_tick(struct rails *r, uint32_t now_ms, uint16_t batt_mv);
bool rails_is_on(const struct rails *r, uint8_t rail);
bool rails_all_settled(const struct rails *r);

#endif /* HN_RAILS_H_ */
