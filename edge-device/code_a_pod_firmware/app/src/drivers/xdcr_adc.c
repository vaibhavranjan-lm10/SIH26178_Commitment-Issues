/*
 * Class C driver: analogue transducer into the MCU ADC (blueprint §3.1).
 * Returns millivolts at the ADC pin, flagged RAW; conversion to the
 * parameter's unit is the calibration stage (§4.3), not this driver.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <errno.h>
#include "xdcr_drivers.h"

static int adc_xdcr_init(const struct xdcr_desc *d)
{
	const struct xdcr_adc_ctx *ctx = d->ctx;

	for (uint8_t i = 0; i < ctx->n_ch; i++) {
		int rc;

		if (!adc_is_ready_dt(&ctx->spec[i])) {
			return -ENODEV;
		}
		rc = adc_channel_setup_dt(&ctx->spec[i]);
		if (rc) {
			return rc;
		}
	}
	return 0;
}

static int adc_xdcr_read(const struct xdcr_desc *d, struct xdcr_sample *out, size_t max)
{
	const struct xdcr_adc_ctx *ctx = d->ctx;
	uint8_t n = ctx->n_ch < max ? ctx->n_ch : (uint8_t)max;

	for (uint8_t i = 0; i < n; i++) {
		int16_t raw = 0;
		struct adc_sequence seq = {
			.buffer = &raw,
			.buffer_size = sizeof(raw),
		};
		int32_t val;
		int rc;

		rc = adc_sequence_init_dt(&ctx->spec[i], &seq);
		if (rc) {
			return rc;
		}
		rc = adc_read_dt(&ctx->spec[i], &seq);
		if (rc) {
			return rc;
		}
		val = raw;
		if (adc_raw_to_millivolts_dt(&ctx->spec[i], &val) != 0) {
			val = raw; /* no vref known: leave as counts, still RAW */
		}
		out[i].param = d->params[i];
		out[i].value = val;
		out[i].flags = XDCR_F_VALID | XDCR_F_RAW;
	}
	return n;
}

const struct xdcr_ops xdcr_adc_ops = {
	.init = adc_xdcr_init,
	.read = adc_xdcr_read,
};
