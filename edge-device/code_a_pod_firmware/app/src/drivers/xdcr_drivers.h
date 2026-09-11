/* Class-driver contexts and ops, instantiated from devicetree in pod_dt_tables.c.
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef PRAHARI_XDCR_DRIVERS_H_
#define PRAHARI_XDCR_DRIVERS_H_

#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <prahari/pulse.h>
#include <prahari/xdcr.h>

#define XDCR_ADC_MAX_CH 3
#define XDCR_SENSOR_MAX_CH 3

/* Class C */
struct xdcr_adc_ctx {
	struct adc_dt_spec spec[XDCR_ADC_MAX_CH];
	uint8_t n_ch;
};
extern const struct xdcr_ops xdcr_adc_ops;

/* Class D.  Order matches the 'mode' enum in prahari,pulse-transducer.yaml. */
enum xdcr_pulse_mode {
	XDCR_PULSE_COUNT = 0,
	XDCR_PULSE_RATE = 1,
};
struct xdcr_pulse_ctx {
	struct gpio_dt_spec gpio;
	struct gpio_callback cb;
	struct pulse_counter pc;
	uint16_t debounce_ms;
	uint8_t mode;
};
extern const struct xdcr_ops xdcr_pulse_ops;

/* Class A / B via Zephyr sensor API */
struct xdcr_sensor_ctx {
	const struct device *dev;
	uint16_t chan[XDCR_SENSOR_MAX_CH];
	uint8_t n_ch;
};
extern const struct xdcr_ops xdcr_sensor_ops;

#endif /* PRAHARI_XDCR_DRIVERS_H_ */
