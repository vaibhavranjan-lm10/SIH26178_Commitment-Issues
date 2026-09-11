/*
 * Class A (I2C) / class B (UART) driver: any Zephyr sensor device.
 * Values are converted from the Zephyr sensor SI unit to milli-units of
 * the PRAHARI parameter unit (kPa -> hPa for P17, m/s^2 -> mg for
 * P42-44; the rest already match), so they are NOT flagged RAW.  The
 * §4.3 two-point calibration still applies on top when a table entry
 * exists for the parameter.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <errno.h>
#include "xdcr_drivers.h"

static int sensor_xdcr_init(const struct xdcr_desc *d)
{
	const struct xdcr_sensor_ctx *ctx = d->ctx;

	return device_is_ready(ctx->dev) ? 0 : -ENODEV;
}

static int32_t to_param_units(uint16_t chan, int64_t milli)
{
	switch (chan) {
	case SENSOR_CHAN_PRESS:
		return (int32_t)(milli * 10); /* m-kPa -> m-hPa */
	case SENSOR_CHAN_ACCEL_X:
	case SENSOR_CHAN_ACCEL_Y:
	case SENSOR_CHAN_ACCEL_Z:
		return (int32_t)((milli * 100000) / 980665); /* m-(m/s^2) -> mg */
	default:
		return (int32_t)milli;
	}
}

static int sensor_xdcr_read(const struct xdcr_desc *d, struct xdcr_sample *out, size_t max)
{
	const struct xdcr_sensor_ctx *ctx = d->ctx;
	uint8_t n = ctx->n_ch < max ? ctx->n_ch : (uint8_t)max;
	int rc;

	rc = sensor_sample_fetch(ctx->dev);
	if (rc) {
		return rc;
	}
	for (uint8_t i = 0; i < n; i++) {
		struct sensor_value v;

		rc = sensor_channel_get(ctx->dev, (enum sensor_channel)ctx->chan[i], &v);
		if (rc) {
			return rc;
		}
		out[i].param = d->params[i];
		out[i].value = to_param_units(ctx->chan[i], sensor_value_to_milli(&v));
		out[i].flags = XDCR_F_VALID;
	}
	return n;
}

const struct xdcr_ops xdcr_sensor_ops = {
	.init = sensor_xdcr_init,
	.read = sensor_xdcr_read,
};
