/*
 * Class D driver: pulse / interrupt transducer (blueprint §3.1).
 *  count mode -> cumulative accepted edges since boot (P12 is an
 *                ACCUMULATION; the head node differences it)
 *  rate mode  -> edges since the previous read
 * Values are counts, flagged RAW; tips->mm and Hz->m/s are calibration.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include "xdcr_drivers.h"

static void pulse_isr(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
	struct xdcr_pulse_ctx *ctx = CONTAINER_OF(cb, struct xdcr_pulse_ctx, cb);

	ARG_UNUSED(port);
	ARG_UNUSED(pins);
	(void)pulse_counter_edge(&ctx->pc, k_uptime_get_32());
}

static int pulse_xdcr_init(const struct xdcr_desc *d)
{
	struct xdcr_pulse_ctx *ctx = d->ctx;
	int rc;

	if (!gpio_is_ready_dt(&ctx->gpio)) {
		return -ENODEV;
	}
	pulse_counter_init(&ctx->pc, ctx->debounce_ms);
	rc = gpio_pin_configure_dt(&ctx->gpio, GPIO_INPUT);
	if (rc) {
		return rc;
	}
	rc = gpio_pin_interrupt_configure_dt(&ctx->gpio, GPIO_INT_EDGE_TO_ACTIVE);
	if (rc) {
		return rc;
	}
	gpio_init_callback(&ctx->cb, pulse_isr, BIT(ctx->gpio.pin));
	return gpio_add_callback_dt(&ctx->gpio, &ctx->cb);
}

static int pulse_xdcr_read(const struct xdcr_desc *d, struct xdcr_sample *out, size_t max)
{
	struct xdcr_pulse_ctx *ctx = d->ctx;
	unsigned int key;
	uint32_t v;

	if (max < 1) {
		return -ENOMEM;
	}
	key = irq_lock();
	v = (ctx->mode == XDCR_PULSE_RATE) ? pulse_counter_take(&ctx->pc)
					   : pulse_counter_total(&ctx->pc);
	irq_unlock(key);

	out[0].param = d->params[0];
	out[0].value = (int32_t)v;
	out[0].flags = XDCR_F_VALID | XDCR_F_RAW;
	return 1;
}

const struct xdcr_ops xdcr_pulse_ops = {
	.init = pulse_xdcr_init,
	.read = pulse_xdcr_read,
};
