/* Siren / relay GPIO (§6.8 actuator only).
 * SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/drivers/gpio.h>
#include <hn/hw.h>

static const struct gpio_dt_spec siren = GPIO_DT_SPEC_GET(DT_NODELABEL(siren), gpios);
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);

int hw_alert_init(void)
{
	int rc;

	if (!gpio_is_ready_dt(&siren) || !gpio_is_ready_dt(&led)) {
		return -ENODEV;
	}
	rc = gpio_pin_configure_dt(&siren, GPIO_OUTPUT_INACTIVE);
	if (rc) {
		return rc;
	}
	return gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
}

void hw_alert_set(bool on)
{
	(void)gpio_pin_set_dt(&siren, on ? 1 : 0);
	(void)gpio_pin_set_dt(&led, on ? 1 : 0);
}
