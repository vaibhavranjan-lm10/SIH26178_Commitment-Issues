/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <string.h>
#include <hn/rails.h>

int rails_init(struct rails *r, const struct rail_cfg *cfg, uint8_t n,
	       int (*set)(void *, uint8_t, bool), void *ctx, uint32_t now_ms)
{
	if (!r || !cfg || !set || n > RAILS_MAX) {
		return -EINVAL;
	}
	memset(r, 0, sizeof(*r));
	r->cfg = cfg;
	r->n = n;
	r->set = set;
	r->ctx = ctx;
	for (uint8_t i = 0; i < n; i++) {
		if (cfg[i].shed_mv && cfg[i].restore_mv <= cfg[i].shed_mv) {
			return -EINVAL;
		}
		r->st[i] = RAIL_OFF;
		r->t_change[i] = now_ms;
		(void)set(ctx, i, false); /* defined state: everything off */
	}
	return 0;
}

void rails_request(struct rails *r, uint8_t rail, bool on)
{
	if (rail < r->n) {
		r->wanted[rail] = on;
	}
}

static void turn(struct rails *r, uint8_t i, enum rail_state st, bool on, uint32_t now)
{
	(void)r->set(r->ctx, i, on);
	r->st[i] = st;
	r->t_change[i] = now;
}

void rails_tick(struct rails *r, uint32_t now_ms, uint16_t batt_mv)
{
	bool sequencing = false;

	for (uint8_t i = 0; i < r->n; i++) {
		const struct rail_cfg *c = &r->cfg[i];

		/* settle timer */
		if (r->st[i] == RAIL_SETTLING) {
			if ((uint32_t)(now_ms - r->t_change[i]) >= c->settle_ms) {
				r->st[i] = RAIL_ON;
			} else {
				sequencing = true; /* one rail at a time */
			}
		}

		/* shedding: lowest battery first sheds highest priority */
		if (c->shed_mv && (r->st[i] == RAIL_ON || r->st[i] == RAIL_SETTLING) &&
		    batt_mv < c->shed_mv) {
			turn(r, i, RAIL_SHED, false, now_ms);
			r->sheds++;
			continue;
		}
		if (r->st[i] == RAIL_SHED) {
			if (batt_mv >= c->restore_mv && r->wanted[i]) {
				r->st[i] = RAIL_OFF; /* eligible for orderly bring-up */
				r->restores++;
			} else {
				continue;
			}
		}

		/* orderly bring-up / shutdown */
		if (r->wanted[i] && r->st[i] == RAIL_OFF) {
			if (sequencing) {
				continue; /* wait for the previous rail to settle */
			}
			if (c->shed_mv && batt_mv < c->restore_mv) {
				continue; /* not enough battery to bring it up */
			}
			turn(r, i, c->settle_ms ? RAIL_SETTLING : RAIL_ON, true, now_ms);
			sequencing = c->settle_ms != 0;
		} else if (!r->wanted[i] && (r->st[i] == RAIL_ON || r->st[i] == RAIL_SETTLING)) {
			turn(r, i, RAIL_OFF, false, now_ms);
		}
	}
}

bool rails_is_on(const struct rails *r, uint8_t rail)
{
	return rail < r->n && (r->st[rail] == RAIL_ON || r->st[rail] == RAIL_SETTLING);
}

bool rails_all_settled(const struct rails *r)
{
	for (uint8_t i = 0; i < r->n; i++) {
		if (r->wanted[i] != (r->st[i] == RAIL_ON) && r->st[i] != RAIL_SHED) {
			return false;
		}
	}
	return true;
}
