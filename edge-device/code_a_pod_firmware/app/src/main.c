/*
 * PRAHARI Code A — pod firmware entry.
 *
 * Boot: resolve position (EEPROM block, else Kconfig) -> bring every load
 * switch OFF -> select the transducers populated at this position ->
 * init them -> loop: tick the power controller once a second, run an
 * acquisition cycle every CONFIG_PRAHARI_SAMPLE_PERIOD_S, read periodic
 * rails (particulate fan) at the end of their own duty window.
 *
 * Each frame then goes through the §4.3 pipeline (calibrate, median-of-5,
 * range gate, z-score gate, derivations, self-report).  The resulting
 * report is printed on the console as the stand-in for the RS-485 / LoRa
 * transport, which is not here yet.
 *
 * Hard boundary: the only history in this image is the anomaly gate's
 * short window.  Antecedent indices, fire-weather cascade, tendencies
 * belong to Code B.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/eeprom.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/printk.h>
#include <prahari/calib.h>
#include <prahari/link.h>
#include <prahari/link_cfg.h>
#include <prahari/pipeline.h>
#include <prahari/params.h>
#include <prahari/pod_position.h>
#include <prahari/power.h>
#include <prahari/sampler.h>
#include <prahari/xdcr.h>
#include "pod_dt_tables.h"

#if defined(CONFIG_PRAHARI_POD_POSITION_DEFAULT_S1)
#define POD_POS_DEFAULT POD_POS_S1
#elif defined(CONFIG_PRAHARI_POD_POSITION_DEFAULT_S2)
#define POD_POS_DEFAULT POD_POS_S2
#elif defined(CONFIG_PRAHARI_POD_POSITION_DEFAULT_S3)
#define POD_POS_DEFAULT POD_POS_S3
#elif defined(CONFIG_PRAHARI_POD_POSITION_DEFAULT_G)
#define POD_POS_DEFAULT POD_POS_G
#elif defined(CONFIG_PRAHARI_POD_POSITION_DEFAULT_U)
#define POD_POS_DEFAULT POD_POS_U
#else
#define POD_POS_DEFAULT POD_POS_C
#endif

#define EEPROM_NODE DT_ALIAS(eeprom_0)
#define HAVE_EEPROM_POSITION                                                                       \
	(IS_ENABLED(CONFIG_PRAHARI_POD_POSITION_FROM_EEPROM) && DT_NODE_HAS_STATUS_OKAY(EEPROM_NODE))

#define VREF_NODE DT_NODELABEL(vref)
#define HAVE_VREF DT_NODE_HAS_STATUS_OKAY(VREF_NODE)

static struct pwr_ctl pwr;
static struct pod_frame frame;
static const struct xdcr_desc *selected[POD_XDCR_COUNT];
static struct sampler sampler;
static struct calib_table calib;
static struct pipeline pipeline;
static struct pod_report report;
static uint16_t init_fail_bits;
static struct link_identity link_id;

static uint32_t clk_now(void *ctx)
{
	ARG_UNUSED(ctx);
	return k_uptime_get_32();
}

static void clk_sleep(void *ctx, uint32_t ms)
{
	ARG_UNUSED(ctx);
	k_sleep(K_MSEC(ms));
}

static const struct sampler_clock clock = {.now_ms = clk_now, .sleep_ms = clk_sleep};

static void on_pwr_event(void *ctx, uint8_t dom, enum pwr_event evt)
{
	struct sampler *s = ctx;

	switch (evt) {
	case PWR_EVT_READY:
		if (pod_pwr_domains[dom].policy == PWR_POLICY_PERIODIC) {
			sampler_on_domain_ready(s, dom);
		}
		break;
	case PWR_EVT_TIMEOUT:
		printk("pwr: %s exceeded on-time cap, forced off\n", pod_pwr_domains[dom].name);
		break;
	case PWR_EVT_DEFERRED:
		printk("pwr: %s window deferred (rail budget)\n", pod_pwr_domains[dom].name);
		break;
	default:
		break;
	}
}

/* Head-node command hook (blueprint §3.4: MOX heater on only when armed). */
int pod_gas_arm(bool armed)
{
	int rc = 0;
	uint32_t now = k_uptime_get_32();

	for (uint8_t d = 0; d < POD_PWR_DOMAIN_COUNT; d++) {
		if (pod_pwr_domains[d].policy == PWR_POLICY_ARMED) {
			int r = pwr_arm(&pwr, d, armed, now);

			if (r && !rc) {
				rc = r;
			}
		}
	}
	return rc;
}


static void load_calibration(void)
{
	int rc = -ENOENT;

#if HAVE_EEPROM_POSITION
	const struct device *eep = DEVICE_DT_GET(EEPROM_NODE);
	uint8_t blob[CALIB_BLOB_MAX];

	if (device_is_ready(eep) &&
	    eeprom_read(eep, CONFIG_PRAHARI_EEPROM_CALIB_OFFSET, blob, sizeof(blob)) == 0) {
		rc = calib_table_decode(blob, sizeof(blob), &calib);
	}
#endif
	if (rc == 0) {
		printk("calibration: %u entries from eeprom\n", calib.n);
	} else {
		calib.n = 0;
		printk("calibration: none (%d); raw channels will be marked UNCAL\n", rc);
	}
}

/* §4.3 self-report: own supply rail through the internal reference. */
static bool read_rail_mv(int32_t *mv)
{
#if HAVE_VREF
	const struct device *vref = DEVICE_DT_GET(VREF_NODE);
	struct sensor_value v;

	if (device_is_ready(vref) && sensor_sample_fetch(vref) == 0 &&
	    sensor_channel_get(vref, SENSOR_CHAN_VOLTAGE, &v) == 0) {
		*mv = (int32_t)sensor_value_to_milli(&v);
		return true;
	}
#endif
	*mv = 0;
	return false;
}

static void link_arm(void *ctx, bool armed)
{
	ARG_UNUSED(ctx);
	(void)pod_gas_arm(armed);
}

static void load_link_identity(enum pod_position pos)
{
	const struct link_cfg dflt = {.addr = CONFIG_PRAHARI_LINK_ADDR,
				      .slot = CONFIG_PRAHARI_LINK_SLOT};
	struct link_cfg c;
	enum link_cfg_source src = LINK_CFG_SRC_KCONFIG;
	const uint8_t *bp = NULL;
	uint8_t blob[LINK_CFG_BLOB_LEN];

#if HAVE_EEPROM_POSITION
	const struct device *eep = DEVICE_DT_GET(EEPROM_NODE);

	if (device_is_ready(eep) &&
	    eeprom_read(eep, LINK_CFG_EEPROM_OFFSET, blob, sizeof(blob)) == 0) {
		bp = blob;
	}
#endif
	(void)link_cfg_resolve(bp, bp ? sizeof(blob) : 0, &dflt, &c, &src);
	link_id.addr = c.addr;
	link_id.slot = c.slot;
	link_id.position = (uint8_t)pos;
	printk("link: %s addr=%u slot=%u (%s)\n", link_mode_name(), c.addr, c.slot,
	       src == LINK_CFG_SRC_EEPROM ? "eeprom" : "kconfig default");
}

static void report_print(const struct pod_report *r)
{
	if (!IS_ENABLED(CONFIG_PRAHARI_FRAME_PRINT)) {
		return;
	}
	printk("report %u%s:", r->primary.cycle, r->wake ? " WAKE" : "");
	for (uint8_t p = 1; p <= PRAHARI_PARAM_INSITU_COUNT; p++) {
		uint8_t f = r->primary.flags[p];

		if (f & XDCR_F_VALID) {
			printk(" P%u=%d%s%s%s%s", p, r->primary.value[p],
			       (f & XDCR_F_UNCAL) ? "u" : "", (f & XDCR_F_RANGE) ? "R" : "",
			       (f & XDCR_F_ANOMALY) ? "!" : "", (f & XDCR_F_STALE) ? "s" : "");
		} else if (f) {
			printk(" P%u=%s", p, (f & XDCR_F_FAULT) ? "FAULT" : "NOPWR");
		}
	}
	for (int d = 0; d < DER_COUNT; d++) {
		if (r->derived_flags[d] & XDCR_F_VALID) {
			printk(" %s=%d%s", pod_derived_name(d), r->derived[d],
			       (r->derived_flags[d] & XDCR_F_RANGE) ? "R" : "");
		}
	}
	printk("  rail=%dmV%s fault=%x nopwr=%x range=%x load=%umA\n", r->self.rail_mv,
	       r->self.rail_valid ? "" : "?", r->self.xdcr_fault, r->self.xdcr_nopower,
	       r->self.xdcr_range, pwr_load_ma(&pwr));
}

static enum pod_position boot_position(enum pod_position_source *src)
{
	uint8_t blob[POD_CFG_BLOB_LEN];
	const uint8_t *bp = NULL;
	enum pod_position pos;

#if HAVE_EEPROM_POSITION
	const struct device *eep = DEVICE_DT_GET(EEPROM_NODE);

	if (device_is_ready(eep) && eeprom_read(eep, 0, blob, sizeof(blob)) == 0) {
		bp = blob;
	}
#endif
	(void)pod_position_resolve(bp, bp ? sizeof(blob) : 0, POD_POS_DEFAULT, &pos, src);
	return pos;
}

int main(void)
{
	enum pod_position_source src;
	enum pod_position pos = boot_position(&src);
	uint32_t now = k_uptime_get_32();
	uint32_t last_cycle = now;
	size_t n;
	int rc;

	printk("PRAHARI pod firmware, position %s (%s)\n", pod_position_name(pos),
	       src == POD_POS_SRC_EEPROM ? "eeprom" : "kconfig default");

	rc = pod_pwr_gpio_init();
	if (rc) {
		printk("load-switch gpio init failed: %d\n", rc);
	}
	rc = pwr_init(&pwr, pod_pwr_domains, pod_pwr_states, POD_PWR_DOMAIN_COUNT, &pod_pwr_backend,
		      CONFIG_PRAHARI_RAIL_BUDGET_MA, now);
	if (rc) {
		printk("power init failed: %d\n", rc);
	}

	n = xdcr_select(pod_xdcrs, POD_XDCR_COUNT, pos, selected, POD_XDCR_COUNT);
	printk("%u of %u transducers populated at %s:\n", (unsigned)n, (unsigned)POD_XDCR_COUNT,
	       pod_position_name(pos));
	for (size_t i = 0; i < n; i++) {
		const struct xdcr_desc *d = selected[i];

		rc = d->ops->init(d);
		if (rc && i < 16) {
			init_fail_bits |= (uint16_t)(1u << i);
		}
		printk("  %-16s %-8s rail=%s init=%d\n", d->name, xdcr_class_name(d->cls),
		       d->domain == XDCR_DOMAIN_NONE ? "passive" : pod_pwr_domains[d->domain].name,
		       rc);
	}

	sampler_init(&sampler, selected, n, &pwr, &clock, &frame);
	pwr_set_event_cb(&pwr, on_pwr_event, &sampler);

	load_calibration();
	{
		const struct pipeline_cfg pcfg = {
			.calib = &calib,
			.ranges = pod_ranges,
			.n_ranges = pod_range_count,
			.z_thr_centi = CONFIG_PRAHARI_ZGATE_THRESHOLD_CENTI,
			.z_floor_permille = CONFIG_PRAHARI_ZGATE_STD_FLOOR_PERMILLE,
		};

		rc = pipeline_init(&pipeline, &pcfg, selected, n);
		if (rc) {
			printk("pipeline init failed: %d (raise PRAHARI_MAX_CHANNELS)\n", rc);
		}
	}

	if (IS_ENABLED(CONFIG_PRAHARI_MOX_ARMED_AT_BOOT)) {
		(void)pod_gas_arm(true);
	}

	load_link_identity(pos);
	rc = link_init(&link_id, link_arm, NULL);
	if (rc) {
		printk("link init failed: %d (pod still senses; nothing leaves)\n", rc);
	}

	while (1) {
		now = k_uptime_get_32();
		pwr_tick(&pwr, now);
		if ((uint32_t)(now - last_cycle) >= (uint32_t)CONFIG_PRAHARI_SAMPLE_PERIOD_S * 1000u) {
			last_cycle = now;
			int32_t rail_mv;
			bool rail_ok;

			(void)sampler_cycle(&sampler);
			rail_ok = read_rail_mv(&rail_mv);
			pipeline_run(&pipeline, &frame, selected, n, rail_mv, rail_ok,
				     init_fail_bits, &report);
			report_print(&report);
			(void)link_publish(&report);
		}
		link_service(k_uptime_get_32());
		link_wait(K_MSEC(1000));
	}
	return 0;
}
