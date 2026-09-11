/* GNSS (NMEA over USART3) for UTC-of-second, PPS edge on a GPIO with a
 * cycle-counter timestamp.  §6.3: the pulse is the point; the fix is a
 * bonus.  Also owns the software-extended microsecond clock.
 * SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/device.h>
#include <zephyr/drivers/gnss.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/timeutil.h>
#include <hn/hw.h>

/* ---- 64-bit cycle extension -> microseconds ---- */
static struct k_spinlock clk_lock;
static uint32_t last_cyc;
static uint64_t cyc_hi;

static uint64_t cycles64(void)
{
	k_spinlock_key_t k = k_spin_lock(&clk_lock);
	uint32_t c = k_cycle_get_32();

	if (c < last_cyc) {
		cyc_hi += (uint64_t)1 << 32;
	}
	last_cyc = c;
	{
		uint64_t v = cyc_hi | c;

		k_spin_unlock(&clk_lock, k);
		return v;
	}
}

uint32_t hw_now_us(void)
{
	return (uint32_t)(cycles64() / (sys_clock_hw_cycles_per_sec() / 1000000u));
}

void hw_clock_service(void)
{
	(void)cycles64(); /* keep the extension fresh (called every 10 ms) */
}

/* ---- PPS ---- */
#define PPS_NODE DT_NODELABEL(pps_in)
static const struct gpio_dt_spec pps_gpio = GPIO_DT_SPEC_GET(PPS_NODE, gpios);
static struct gpio_callback pps_cb;
static struct pps_disc *disc;
static volatile bool fix_valid;

static void pps_isr(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);
	if (disc) {
		(void)pps_edge(disc, hw_now_us());
	}
}

/* ---- GNSS data: the UTC time of the most recent fix ---- */
#define GNSS_NODE DT_CHOSEN(prahari_gnss)

static void gnss_data_cb(const struct device *dev, const struct gnss_data *data)
{
	struct tm t = {0};
	int64_t utc;

	ARG_UNUSED(dev);
	fix_valid = data->info.fix_status != GNSS_FIX_STATUS_NO_FIX;
	if (!disc || data->utc.century_year == 0) {
		return;
	}
	t.tm_year = 100 + data->utc.century_year; /* 20xx - 1900 */
	t.tm_mon = data->utc.month - 1;
	t.tm_mday = data->utc.month_day;
	t.tm_hour = data->utc.hour;
	t.tm_min = data->utc.minute;
	t.tm_sec = (int)(data->utc.millisecond / 1000);
	utc = timeutil_timegm64(&t);
	/* The NMEA sentence describes the second that began at the last PPS edge. */
	pps_set_utc(disc, (uint64_t)utc);
}

GNSS_DT_DATA_CALLBACK_DEFINE(GNSS_NODE, gnss_data_cb);

int hw_gnss_init(struct pps_disc *d)
{
	int rc;

	disc = d;
	if (!gpio_is_ready_dt(&pps_gpio)) {
		return -ENODEV;
	}
	rc = gpio_pin_configure_dt(&pps_gpio, GPIO_INPUT);
	if (rc) {
		return rc;
	}
	rc = gpio_pin_interrupt_configure_dt(&pps_gpio, GPIO_INT_EDGE_TO_ACTIVE);
	if (rc) {
		return rc;
	}
	gpio_init_callback(&pps_cb, pps_isr, BIT(pps_gpio.pin));
	rc = gpio_add_callback_dt(&pps_gpio, &pps_cb);
	if (rc) {
		return rc;
	}
	return device_is_ready(DEVICE_DT_GET(GNSS_NODE)) ? 0 : -ENODEV;
}

bool hw_gnss_has_fix(void)
{
	return fix_valid;
}
