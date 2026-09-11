/* Independent watchdog (IWDG).  Fed only by the supervisor's verdict.
 * SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/device.h>
#include <zephyr/drivers/watchdog.h>
#include <hn/hw.h>

static const struct device *const wdt = DEVICE_DT_GET(DT_ALIAS(watchdog0));
static int channel = -1;

int hw_wdt_init(uint32_t timeout_ms)
{
	struct wdt_timeout_cfg cfg = {
		.window = {.min = 0, .max = timeout_ms},
		.flags = WDT_FLAG_RESET_SOC,
	};

	if (!device_is_ready(wdt)) {
		return -ENODEV;
	}
	channel = wdt_install_timeout(wdt, &cfg);
	if (channel < 0) {
		return channel;
	}
	return wdt_setup(wdt, WDT_OPT_PAUSE_HALTED_BY_DBG);
}

void hw_wdt_feed(void)
{
	if (channel >= 0) {
		(void)wdt_feed(wdt, channel);
	}
}
