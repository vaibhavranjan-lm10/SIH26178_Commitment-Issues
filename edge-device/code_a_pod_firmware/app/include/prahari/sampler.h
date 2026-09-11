/*
 * Acquisition sequencer: walks the selected transducers, brings their rail
 * up, waits for settle, reads, releases, and fills the pod frame.  Pure
 * (clock injected) so the whole power-around-read behaviour is unit-tested.
 *
 * Frame semantics: a parameter without XDCR_F_VALID is MASKED.  Nothing in
 * this file ever fabricates a value for a channel that was not read.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef PRAHARI_SAMPLER_H_
#define PRAHARI_SAMPLER_H_

#include <stddef.h>
#include <stdint.h>
#include <prahari/params.h>
#include <prahari/power.h>
#include <prahari/xdcr.h>

struct pod_frame {
	int32_t value[PRAHARI_PARAM_INSITU_COUNT + 1]; /* indexed by P-ID */
	uint8_t flags[PRAHARI_PARAM_INSITU_COUNT + 1];
	uint32_t cycle;
};

struct sampler_clock {
	uint32_t (*now_ms)(void *ctx);
	void (*sleep_ms)(void *ctx, uint32_t ms);
	void *ctx;
};

struct sampler {
	const struct xdcr_desc *const *sel;
	size_t n;
	struct pwr_ctl *pwr;
	const struct sampler_clock *clk;
	struct pod_frame *frame;
};

void sampler_init(struct sampler *s, const struct xdcr_desc *const *sel, size_t n,
		  struct pwr_ctl *pwr, const struct sampler_clock *clk, struct pod_frame *frame);

/* One cadence cycle: every selected transducer except those on a
 * 'periodic' rail (which are read from sampler_on_domain_ready). */
int sampler_cycle(struct sampler *s);

/* Read one transducer now, managing its rail per policy. */
int sampler_read_desc(struct sampler *s, const struct xdcr_desc *d);

/* Call from the PWR_EVT_READY of a periodic domain. */
void sampler_on_domain_ready(struct sampler *s, uint8_t dom);

static inline int pod_frame_is_masked(const struct pod_frame *f, uint8_t p)
{
	return !(f->flags[p] & XDCR_F_VALID);
}

#endif /* PRAHARI_SAMPLER_H_ */
