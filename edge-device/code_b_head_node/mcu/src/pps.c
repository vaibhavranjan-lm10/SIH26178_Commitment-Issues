/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <string.h>
#include <hn/pps.h>

#define NOMINAL_US 1000000u

void pps_init(struct pps_disc *d, uint32_t tol_us, uint8_t lock_pulses, uint32_t holdover_us)
{
	memset(d, 0, sizeof(*d));
	d->nominal_us = NOMINAL_US;
	d->tol_us = tol_us;
	d->lock_pulses = lock_pulses ? lock_pulses : 1;
	d->holdover_us = holdover_us;
	d->period_us = NOMINAL_US;
	d->period_frac = 0;
	d->state = PPS_UNLOCKED;
}

int pps_edge(struct pps_disc *d, uint32_t t_us)
{
	uint32_t dt;
	int64_t err;

	if (!d->have_edge) {
		d->have_edge = true;
		d->last_edge_us = t_us;
		d->pulses++;
		return 1;
	}
	dt = t_us - d->last_edge_us; /* wrap-safe */
	err = (int64_t)dt - (int64_t)d->nominal_us;
	if (err > (int64_t)d->tol_us || err < -(int64_t)d->tol_us) {
		/* Could be a missed pulse (dt ~ 2 s) or a glitch (dt tiny).
		 * A missed pulse still tells us where the second boundary is;
		 * a glitch does not.  Accept multiples of the nominal period. */
		uint32_t n = (dt + d->nominal_us / 2) / d->nominal_us;
		int64_t err_n = (int64_t)dt - (int64_t)n * d->nominal_us;

		if (n < 2 || n > 60 || err_n > (int64_t)d->tol_us * (int64_t)n ||
		    err_n < -(int64_t)d->tol_us * (int64_t)n) {
			d->glitches++;
			d->good_run = 0;
			return 0;
		}
		/* n whole seconds elapsed */
		if (d->utc_valid) {
			d->utc_s_at_edge += n;
		}
		d->last_edge_us = t_us;
		d->pulses++;
		return 1;
	}

	/* Good pulse: EMA of the measured period, alpha = 1/8, in 1/65536 us. */
	{
		uint64_t cur = ((uint64_t)d->period_us << 16) | d->period_frac;
		uint64_t meas = (uint64_t)dt << 16;
		uint64_t upd = cur - (cur >> 3) + (meas >> 3);

		d->period_us = (uint32_t)(upd >> 16);
		d->period_frac = (uint32_t)(upd & 0xFFFF);
	}
	if (d->utc_valid) {
		d->utc_s_at_edge += 1;
	}
	d->last_edge_us = t_us;
	d->pulses++;
	if (d->good_run < 255) {
		d->good_run++;
	}
	if (d->good_run >= d->lock_pulses && d->utc_valid) {
		d->state = PPS_LOCKED;
	}
	return 1;
}

void pps_set_utc(struct pps_disc *d, uint64_t utc_s)
{
	d->utc_s_at_edge = utc_s;
	d->utc_valid = d->have_edge;
	if (d->utc_valid && d->good_run >= d->lock_pulses) {
		d->state = PPS_LOCKED;
	}
}

enum pps_state pps_tick(struct pps_disc *d, uint32_t now_us)
{
	if (d->state == PPS_LOCKED && d->holdover_us &&
	    (uint32_t)(now_us - d->last_edge_us) > d->holdover_us) {
		d->state = PPS_HOLDOVER;
		d->good_run = 0;
	}
	return d->state;
}

int pps_local_to_utc_ms(const struct pps_disc *d, uint32_t t_us, uint64_t *utc_ms)
{
	int64_t dt_us, ms;
	uint64_t period_q16;

	if (!d->utc_valid || d->state == PPS_UNLOCKED) {
		return -EAGAIN;
	}
	dt_us = (int32_t)(t_us - d->last_edge_us); /* signed: t may precede the edge */
	period_q16 = ((uint64_t)d->period_us << 16) | d->period_frac;
	/* true seconds = dt_us / period; ms = dt_us * 1000 * 65536 / period_q16.
	 * |dt_us| < 2^31, so the numerator stays under 2^57: int64 is enough. */
	ms = (dt_us * 1000LL * 65536LL) / (int64_t)period_q16;
	*utc_ms = (uint64_t)((int64_t)d->utc_s_at_edge * 1000 + ms);
	return 0;
}

int32_t pps_drift_ppm(const struct pps_disc *d)
{
	int64_t q16 = (int64_t)(((uint64_t)d->period_us << 16) | d->period_frac);
	int64_t nom = (int64_t)d->nominal_us << 16;

	return (int32_t)((q16 - nom) * 1000000 / nom);
}
