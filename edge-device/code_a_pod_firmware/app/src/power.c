/*
 * Switched-rail power controller — see include/prahari/power.h.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <errno.h>
#include <string.h>
#include <prahari/power.h>

static inline uint32_t elapsed(uint32_t now, uint32_t then)
{
	return now - then; /* wrap-safe */
}

static void emit(struct pwr_ctl *c, uint8_t dom, enum pwr_event evt)
{
	if (c->cb) {
		c->cb(c->cb_ctx, dom, evt);
	}
}

static int switch_on(struct pwr_ctl *c, uint8_t dom, uint32_t now)
{
	const struct pwr_domain_cfg *cfg = &c->cfg[dom];
	struct pwr_domain_state *st = &c->st[dom];
	int rc;

	if (c->budget_ma && (pwr_load_ma(c) + cfg->current_ma) > c->budget_ma) {
		return -EBUSY;
	}
	rc = c->be->set(c->be->ctx, dom, true);
	if (rc) {
		return rc;
	}
	st->t_on = now;
	st->state = cfg->settle_ms ? PWR_STATE_SETTLING : PWR_STATE_ON;
	if (st->state == PWR_STATE_ON) {
		emit(c, dom, PWR_EVT_READY);
	}
	return 0;
}

static void switch_off(struct pwr_ctl *c, uint8_t dom)
{
	struct pwr_domain_state *st = &c->st[dom];

	(void)c->be->set(c->be->ctx, dom, false);
	st->state = PWR_STATE_OFF;
	st->refs = 0;
	emit(c, dom, PWR_EVT_OFF);
}

int pwr_init(struct pwr_ctl *c, const struct pwr_domain_cfg *cfg, struct pwr_domain_state *st,
	     uint8_t n, const struct pwr_backend *be, uint16_t budget_ma, uint32_t now_ms)
{
	if (!c || !cfg || !st || !be || !be->set) {
		return -EINVAL;
	}
	c->cfg = cfg;
	c->st = st;
	c->n = n;
	c->be = be;
	c->budget_ma = budget_ma;
	c->cb = NULL;
	c->cb_ctx = NULL;
	memset(st, 0, sizeof(*st) * n);

	for (uint8_t d = 0; d < n; d++) {
		int rc = be->set(be->ctx, d, false); /* §3.4: nothing energised */

		if (rc) {
			return rc;
		}
		st[d].state = PWR_STATE_OFF;
		st[d].t_window = now_ms;
		st[d].window_due = (cfg[d].policy == PWR_POLICY_PERIODIC);
	}
	return 0;
}

void pwr_set_event_cb(struct pwr_ctl *c, pwr_event_cb_t cb, void *ctx)
{
	c->cb = cb;
	c->cb_ctx = ctx;
}

int pwr_acquire(struct pwr_ctl *c, uint8_t dom, uint32_t now_ms)
{
	struct pwr_domain_state *st;
	int rc;

	if (dom >= c->n) {
		return -EINVAL;
	}
	st = &c->st[dom];

	switch (c->cfg[dom].policy) {
	case PWR_POLICY_PER_READ:
	case PWR_POLICY_HOLD:
		if (st->state == PWR_STATE_OFF) {
			rc = switch_on(c, dom, now_ms);
			if (rc) {
				return rc;
			}
		}
		break;
	case PWR_POLICY_PERIODIC:
		if (st->state == PWR_STATE_OFF) {
			return -EAGAIN;
		}
		break;
	case PWR_POLICY_ARMED:
		if (!st->armed || st->state == PWR_STATE_OFF) {
			return -EACCES;
		}
		break;
	default:
		return -EINVAL;
	}
	if (st->refs == UINT8_MAX) {
		return -EOVERFLOW;
	}
	st->refs++;
	return 0;
}

int pwr_release(struct pwr_ctl *c, uint8_t dom, uint32_t now_ms)
{
	struct pwr_domain_state *st;

	(void)now_ms;
	if (dom >= c->n) {
		return -EINVAL;
	}
	st = &c->st[dom];
	if (st->refs == 0) {
		return -EINVAL;
	}
	st->refs--;
	if (st->refs == 0 && c->cfg[dom].policy == PWR_POLICY_PER_READ &&
	    st->state != PWR_STATE_OFF) {
		switch_off(c, dom);
	}
	return 0;
}

bool pwr_is_ready(const struct pwr_ctl *c, uint8_t dom, uint32_t now_ms)
{
	const struct pwr_domain_state *st;

	if (dom >= c->n) {
		return false;
	}
	st = &c->st[dom];
	if (st->state == PWR_STATE_ON) {
		return true;
	}
	if (st->state == PWR_STATE_SETTLING) {
		return elapsed(now_ms, st->t_on) >= c->cfg[dom].settle_ms;
	}
	return false;
}

uint32_t pwr_settle_remaining_ms(const struct pwr_ctl *c, uint8_t dom, uint32_t now_ms)
{
	const struct pwr_domain_state *st;
	uint32_t e;

	if (dom >= c->n) {
		return 0;
	}
	st = &c->st[dom];
	if (st->state != PWR_STATE_SETTLING) {
		return 0;
	}
	e = elapsed(now_ms, st->t_on);
	return e >= c->cfg[dom].settle_ms ? 0 : c->cfg[dom].settle_ms - e;
}

int pwr_arm(struct pwr_ctl *c, uint8_t dom, bool armed, uint32_t now_ms)
{
	struct pwr_domain_state *st;

	if (dom >= c->n) {
		return -EINVAL;
	}
	if (c->cfg[dom].policy != PWR_POLICY_ARMED) {
		return -ENOTSUP;
	}
	st = &c->st[dom];
	st->armed = armed;
	if (armed) {
		if (st->state == PWR_STATE_OFF) {
			return switch_on(c, dom, now_ms);
		}
		return 0;
	}
	if (st->state != PWR_STATE_OFF) {
		switch_off(c, dom);
	}
	return 0;
}

void pwr_tick_domain(struct pwr_ctl *c, uint8_t d, uint32_t now_ms)
{
	const struct pwr_domain_cfg *cfg;
	struct pwr_domain_state *st;

	if (d >= c->n) {
		return;
	}
	cfg = &c->cfg[d];
	st = &c->st[d];

	/* Settle -> ready. */
	if (st->state == PWR_STATE_SETTLING && elapsed(now_ms, st->t_on) >= cfg->settle_ms) {
		st->state = PWR_STATE_ON;
		emit(c, d, PWR_EVT_READY);
	}

	if (cfg->policy == PWR_POLICY_PERIODIC) {
		if (st->state == PWR_STATE_OFF) {
			if (!st->window_due && elapsed(now_ms, st->t_window) >= cfg->period_ms) {
				st->window_due = true;
			}
			if (st->window_due) {
				if (switch_on(c, d, now_ms) == 0) {
					st->window_due = false;
					st->t_window = now_ms;
				} else {
					emit(c, d, PWR_EVT_DEFERRED);
				}
			}
		} else if (elapsed(now_ms, st->t_on) >= cfg->on_ms) {
			switch_off(c, d);
		}
		return;
	}

	/* On-time cap for the other policies (guards a stuck read). */
	if (cfg->on_ms && st->state != PWR_STATE_OFF && elapsed(now_ms, st->t_on) >= cfg->on_ms) {
		emit(c, d, PWR_EVT_TIMEOUT);
		switch_off(c, d);
	}
}

void pwr_tick(struct pwr_ctl *c, uint32_t now_ms)
{
	for (uint8_t d = 0; d < c->n; d++) {
		pwr_tick_domain(c, d, now_ms);
	}
}

uint32_t pwr_load_ma(const struct pwr_ctl *c)
{
	uint32_t ma = 0;

	for (uint8_t d = 0; d < c->n; d++) {
		if (c->st[d].state != PWR_STATE_OFF) {
			ma += c->cfg[d].current_ma;
		}
	}
	return ma;
}

bool pwr_all_off(const struct pwr_ctl *c)
{
	for (uint8_t d = 0; d < c->n; d++) {
		if (c->st[d].state != PWR_STATE_OFF) {
			return false;
		}
	}
	return true;
}

enum pwr_state pwr_state(const struct pwr_ctl *c, uint8_t dom)
{
	return dom < c->n ? c->st[dom].state : PWR_STATE_OFF;
}
