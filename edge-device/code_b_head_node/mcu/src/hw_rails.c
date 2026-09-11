/* Rail enables on GPIO, battery sense on the ADC.
 * SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <hn/hw.h>

static const struct gpio_dt_spec rails_gpio[] = {
	GPIO_DT_SPEC_GET(DT_NODELABEL(rail_12v), gpios),
	GPIO_DT_SPEC_GET(DT_NODELABEL(rail_radio), gpios),
};
static const struct adc_dt_spec batt = ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), batt);

/* Provisional carrier divider: 4:1 (12.8 V -> 3.2 V at the ADC pin). */
#define BATT_DIVIDER 4

int hw_rails_init(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(rails_gpio); i++) {
		int rc;

		if (!gpio_is_ready_dt(&rails_gpio[i])) {
			return -ENODEV;
		}
		rc = gpio_pin_configure_dt(&rails_gpio[i], GPIO_OUTPUT_INACTIVE);
		if (rc) {
			return rc;
		}
	}
	if (!adc_is_ready_dt(&batt)) {
		return -ENODEV;
	}
	return adc_channel_setup_dt(&batt);
}

int hw_rail_set(void *ctx, uint8_t rail, bool on)
{
	ARG_UNUSED(ctx);
	if (rail >= ARRAY_SIZE(rails_gpio)) {
		return -EINVAL;
	}
	return gpio_pin_set_dt(&rails_gpio[rail], on ? 1 : 0);
}

uint16_t hw_batt_mv(void)
{
	int16_t raw = 0;
	struct adc_sequence seq = {.buffer = &raw, .buffer_size = sizeof(raw)};
	int32_t mv;

	if (adc_sequence_init_dt(&batt, &seq) || adc_read_dt(&batt, &seq)) {
		return 0;
	}
	mv = raw;
	if (adc_raw_to_millivolts_dt(&batt, &mv)) {
		return 0;
	}
	return (uint16_t)(mv * BATT_DIVIDER);
}
