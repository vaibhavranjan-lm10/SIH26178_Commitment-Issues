/*
 * Acquisition sequencer — see include/prahari/sampler.h.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <errno.h>
#include <string.h>
#include <prahari/sampler.h>

#define XDCR_MAX_CHANNELS 4

void sampler_init(struct sampler *s, const struct xdcr_desc *const *sel, size_t n,
		  struct pwr_ctl *pwr, const struct sampler_clock *clk, struct pod_frame *frame)
{
	s->sel = sel;
	s->n = n;
	s->pwr = pwr;
	s->clk = clk;
	s->frame = frame;
	memset(frame, 0, sizeof(*frame));
}

static void mask_params(struct pod_frame *f, const struct xdcr_desc *d, uint8_t flags)
{
	for (uint8_t i = 0; i < d->n_params; i++) {
		uint8_t p = d->params[i];

		if (prahari_param_is_insitu(p)) {
			f->flags[p] = flags; /* never XDCR_F_VALID here */
		}
	}
}

static int desc_reports(const struct xdcr_desc *d, uint8_t p)
{
	for (uint8_t i = 0; i < d->n_params; i++) {
		if (d->params[i] == p) {
			return 1;
		}
	}
	return 0;
}

static enum pwr_policy domain_policy(const struct sampler *s, const struct xdcr_desc *d)
{
	return s->pwr->cfg[d->domain].policy;
}

int sampler_read_desc(struct sampler *s, const struct xdcr_desc *d)
{
	struct xdcr_sample buf[XDCR_MAX_CHANNELS];
	bool manage = d->domain != XDCR_DOMAIN_NONE && domain_policy(s, d) != PWR_POLICY_PERIODIC;
	uint32_t now;
	int n;

	mask_params(s->frame, d, 0);

	if (d->domain != XDCR_DOMAIN_NONE) {
		now = s->clk->now_ms(s->clk->ctx);
		if (manage) {
			int rc = pwr_acquire(s->pwr, (uint8_t)d->domain, now);

			if (rc) {
				mask_params(s->frame, d, XDCR_F_NOPOWER);
				return rc;
			}
		}
		/* Wait out the settle time (blueprint §3.4 per-read excitation). */
		while (!pwr_is_ready(s->pwr, (uint8_t)d->domain, now)) {
			uint32_t rem = pwr_settle_remaining_ms(s->pwr, (uint8_t)d->domain, now);

			s->clk->sleep_ms(s->clk->ctx, rem ? rem : 1);
			now = s->clk->now_ms(s->clk->ctx);
			pwr_tick_domain(s->pwr, (uint8_t)d->domain, now);
			if (pwr_state(s->pwr, (uint8_t)d->domain) == PWR_STATE_OFF) {
				mask_params(s->frame, d, XDCR_F_NOPOWER);
				return -ETIMEDOUT;
			}
		}
	}

	n = d->ops->read(d, buf, XDCR_MAX_CHANNELS);

	if (manage) {
		now = s->clk->now_ms(s->clk->ctx);
		(void)pwr_release(s->pwr, (uint8_t)d->domain, now);
	}

	if (n < 0) {
		mask_params(s->frame, d, XDCR_F_FAULT);
		return n;
	}
	for (int i = 0; i < n; i++) {
		uint8_t p = buf[i].param;

		if (!prahari_param_is_insitu(p) || !desc_reports(d, p) ||
		    !(buf[i].flags & XDCR_F_VALID)) {
			continue; /* unknown or unfilled channel stays masked */
		}
		s->frame->value[p] = buf[i].value;
		s->frame->flags[p] = (uint8_t)(buf[i].flags & ~XDCR_F_STALE) | XDCR_F_VALID;
	}
	return n;
}

int sampler_cycle(struct sampler *s)
{
	int errors = 0;

	s->frame->cycle++;
	for (size_t i = 0; i < s->n; i++) {
		const struct xdcr_desc *d = s->sel[i];

		if (d->domain != XDCR_DOMAIN_NONE && domain_policy(s, d) == PWR_POLICY_PERIODIC) {
			/* Read on its own duty cycle; carry the last value, marked stale. */
			for (uint8_t k = 0; k < d->n_params; k++) {
				uint8_t p = d->params[k];

				if (prahari_param_is_insitu(p) && (s->frame->flags[p] & XDCR_F_VALID)) {
					s->frame->flags[p] |= XDCR_F_STALE;
				}
			}
			continue;
		}
		if (sampler_read_desc(s, d) < 0) {
			errors++;
		}
	}
	return errors;
}

void sampler_on_domain_ready(struct sampler *s, uint8_t dom)
{
	for (size_t i = 0; i < s->n; i++) {
		const struct xdcr_desc *d = s->sel[i];

		if (d->domain == (int8_t)dom) {
			(void)sampler_read_desc(s, d);
		}
	}
}
