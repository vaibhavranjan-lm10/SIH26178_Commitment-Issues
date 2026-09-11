/*
 * Acquisition sequencer + power interplay, with fake transducers, a fake
 * load-switch backend and a fake clock.  Verifies the §3.4 rule end to end:
 * a rail is on only around its read, the rain gauge never touches a switch,
 * and a channel that was not read is masked (never imputed).
 */
#include <string.h>
#include <zephyr/ztest.h>
#include <prahari/pulse.h>
#include <prahari/sampler.h>

enum { D_A, D_SOIL, D_PM, D_MOX, D_N };

static const struct pwr_domain_cfg cfg[D_N] = {
	[D_A] = {"sensor-rail", PWR_POLICY_PER_READ, 1, 20, 5000, 0},
	[D_SOIL] = {"soil-excitation", PWR_POLICY_PER_READ, 10, 100, 200, 0},
	[D_PM] = {"particulate-fan", PWR_POLICY_PERIODIC, 80, 25000, 30000, 900000},
	[D_MOX] = {"mox-heater", PWR_POLICY_ARMED, 40, 30000, 0, 0},
};

/* --- fake clock: sleeping advances time --- */
static uint32_t fake_now;
static uint32_t total_slept;

static uint32_t clk_now(void *ctx)
{
	ARG_UNUSED(ctx);
	return fake_now;
}
static void clk_sleep(void *ctx, uint32_t ms)
{
	ARG_UNUSED(ctx);
	fake_now += ms;
	total_slept += ms;
}
static const struct sampler_clock clk = {.now_ms = clk_now, .sleep_ms = clk_sleep};

/* --- fake backend: log every switch with its time --- */
#define LOG_MAX 64
static struct {
	bool on[D_N];
	int n;
	struct {
		uint8_t dom;
		bool on;
		uint32_t t;
	} log[LOG_MAX];
} be_state;

static int be_set(void *ctx, uint8_t dom, bool on)
{
	ARG_UNUSED(ctx);
	be_state.on[dom] = on;
	if (be_state.n < LOG_MAX) {
		be_state.log[be_state.n].dom = dom;
		be_state.log[be_state.n].on = on;
		be_state.log[be_state.n].t = fake_now;
		be_state.n++;
	}
	return 0;
}
static const struct pwr_backend be = {.set = be_set};

/* --- fake transducers --- */
struct fake_ctx {
	int32_t value;
	int rc;         /* <0: read fails */
	int reads;
	bool rail_on_at_read;
	bool settled_at_read;
	uint8_t dom_to_check;
};

static struct pwr_ctl pwr;

static int fake_init(const struct xdcr_desc *d)
{
	ARG_UNUSED(d);
	return 0;
}

static int fake_read(const struct xdcr_desc *d, struct xdcr_sample *out, size_t max)
{
	struct fake_ctx *f = d->ctx;

	f->reads++;
	if (d->domain != XDCR_DOMAIN_NONE) {
		f->rail_on_at_read = be_state.on[d->domain];
		f->settled_at_read = pwr_is_ready(&pwr, (uint8_t)d->domain, fake_now);
	}
	if (f->rc < 0) {
		return f->rc;
	}
	for (uint8_t i = 0; i < d->n_params && i < max; i++) {
		out[i].param = d->params[i];
		out[i].value = f->value + i;
		out[i].flags = XDCR_F_VALID | XDCR_F_RAW;
	}
	return d->n_params;
}

static const struct xdcr_ops fake_ops = {.init = fake_init, .read = fake_read};

static struct fake_ctx f_soil, f_rain, f_pm, f_mox, f_bad, f_trh;
static const uint8_t p_soil[] = {PRAHARI_P1_SOIL_VWC_10CM};
static const uint8_t p_rain[] = {PRAHARI_P12_RAINFALL_ACCUM};
static const uint8_t p_pm[] = {PRAHARI_P21_PM1_0, PRAHARI_P22_PM2_5, PRAHARI_P23_PM10};
static const uint8_t p_mox[] = {PRAHARI_P31_TVOC};
static const uint8_t p_bad[] = {PRAHARI_P10_SURFACE_SOIL_MOISTURE};
static const uint8_t p_trh[] = {PRAHARI_P13_AIR_TEMP_UNDERSTORY, PRAHARI_P15_RH_UNDERSTORY};

#define FAKE(nm, cl, dm, pr, cx)                                                            \
	{.name = nm, .cls = cl, .position_mask = 0xFF, .domain = dm,                             \
	 .n_params = ARRAY_SIZE(pr), .params = pr, .ops = &fake_ops, .ctx = &cx}

static const struct xdcr_desc x_soil = FAKE("soil", XDCR_CLASS_C_ADC, D_SOIL, p_soil, f_soil);
static const struct xdcr_desc x_rain =
	FAKE("rain", XDCR_CLASS_D_PULSE, XDCR_DOMAIN_NONE, p_rain, f_rain);
static const struct xdcr_desc x_pm = FAKE("pm", XDCR_CLASS_B_UART, D_PM, p_pm, f_pm);
static const struct xdcr_desc x_mox = FAKE("mox", XDCR_CLASS_C_ADC, D_MOX, p_mox, f_mox);
static const struct xdcr_desc x_bad = FAKE("bad", XDCR_CLASS_C_ADC, D_SOIL, p_bad, f_bad);
static const struct xdcr_desc x_trh = FAKE("trh", XDCR_CLASS_A_I2C, D_A, p_trh, f_trh);

static struct pwr_domain_state st[D_N];
static struct pod_frame frame;
static struct sampler smp;

static void on_event(void *ctx, uint8_t dom, enum pwr_event e)
{
	if (e == PWR_EVT_READY && cfg[dom].policy == PWR_POLICY_PERIODIC) {
		sampler_on_domain_ready(ctx, dom);
	}
}

static void start(const struct xdcr_desc *const *sel, size_t n)
{
	fake_now = 1000;
	total_slept = 0;
	memset(&be_state, 0, sizeof(be_state));
	memset(&f_soil, 0, sizeof(f_soil));
	memset(&f_rain, 0, sizeof(f_rain));
	memset(&f_pm, 0, sizeof(f_pm));
	memset(&f_mox, 0, sizeof(f_mox));
	memset(&f_bad, 0, sizeof(f_bad));
	memset(&f_trh, 0, sizeof(f_trh));
	f_soil.value = 1234;
	f_rain.value = 7;
	f_pm.value = 35;
	f_mox.value = 900;
	f_trh.value = 25000;
	f_bad.rc = -EIO;
	zassert_ok(pwr_init(&pwr, cfg, st, D_N, &be, 0, fake_now));
	sampler_init(&smp, sel, n, &pwr, &clk, &frame);
	pwr_set_event_cb(&pwr, on_event, &smp);
}

ZTEST_SUITE(sampler, NULL, NULL, NULL, NULL, NULL);

ZTEST(sampler, test_per_read_rail_on_only_around_read)
{
	const struct xdcr_desc *const sel[] = {&x_soil};

	start(sel, 1);
	zassert_equal(sampler_cycle(&smp), 0);

	zassert_equal(f_soil.reads, 1);
	zassert_true(f_soil.rail_on_at_read, "excitation on during the read");
	zassert_true(f_soil.settled_at_read, "read only after the 100 ms settle");
	zassert_equal(total_slept, 100, "waited exactly the settle time");
	zassert_true(pwr_all_off(&pwr), "§3.4: nothing energised after the read");

	/* Switch log after init: on(soil) then off(soil), nothing else. */
	zassert_equal(be_state.n, D_N + 2);
	zassert_equal(be_state.log[D_N].dom, D_SOIL);
	zassert_true(be_state.log[D_N].on);
	zassert_equal(be_state.log[D_N + 1].dom, D_SOIL);
	zassert_false(be_state.log[D_N + 1].on);
	zassert_true(be_state.log[D_N + 1].t - be_state.log[D_N].t <= cfg[D_SOIL].on_ms,
		     "within the 200 ms per-read budget");

	zassert_false(pod_frame_is_masked(&frame, PRAHARI_P1_SOIL_VWC_10CM));
	zassert_equal(frame.value[PRAHARI_P1_SOIL_VWC_10CM], 1234);
	zassert_true(frame.flags[PRAHARI_P1_SOIL_VWC_10CM] & XDCR_F_RAW);
}

ZTEST(sampler, test_rain_gauge_never_switches_anything)
{
	const struct xdcr_desc *const sel[] = {&x_rain};

	start(sel, 1);
	zassert_equal(sampler_cycle(&smp), 0);
	zassert_equal(be_state.n, D_N, "only the init-off calls; passive gauge untouched");
	zassert_equal(total_slept, 0);
	zassert_false(pod_frame_is_masked(&frame, PRAHARI_P12_RAINFALL_ACCUM));
	zassert_equal(frame.value[PRAHARI_P12_RAINFALL_ACCUM], 7);
}

ZTEST(sampler, test_failed_read_is_masked_not_imputed)
{
	const struct xdcr_desc *const sel[] = {&x_bad, &x_soil};

	start(sel, 2);
	frame.value[PRAHARI_P10_SURFACE_SOIL_MOISTURE] = 4242; /* stale garbage */
	frame.flags[PRAHARI_P10_SURFACE_SOIL_MOISTURE] = XDCR_F_VALID;

	zassert_equal(sampler_cycle(&smp), 1, "one transducer failed");
	zassert_true(pod_frame_is_masked(&frame, PRAHARI_P10_SURFACE_SOIL_MOISTURE));
	zassert_true(frame.flags[PRAHARI_P10_SURFACE_SOIL_MOISTURE] & XDCR_F_FAULT);
	zassert_false(pod_frame_is_masked(&frame, PRAHARI_P1_SOIL_VWC_10CM),
		      "a failing neighbour must not mask a good channel");
	zassert_true(pwr_all_off(&pwr), "rail released even when the read failed");
}

ZTEST(sampler, test_periodic_fan_read_at_window_not_in_cycle)
{
	const struct xdcr_desc *const sel[] = {&x_pm, &x_soil};

	start(sel, 2);

	/* Cadence cycle: PM skipped, still masked; fan not started by the cycle. */
	zassert_equal(sampler_cycle(&smp), 0);
	zassert_equal(f_pm.reads, 0);
	zassert_true(pod_frame_is_masked(&frame, PRAHARI_P22_PM2_5));
	zassert_false(be_state.on[D_PM]);

	/* Tick opens the window; READY at 25 s triggers the read. */
	pwr_tick(&pwr, fake_now);
	zassert_true(be_state.on[D_PM]);
	fake_now += 24999;
	pwr_tick(&pwr, fake_now);
	zassert_equal(f_pm.reads, 0);
	fake_now += 1;
	pwr_tick(&pwr, fake_now);
	zassert_equal(f_pm.reads, 1);
	zassert_true(f_pm.rail_on_at_read);
	zassert_true(f_pm.settled_at_read);
	zassert_false(pod_frame_is_masked(&frame, PRAHARI_P21_PM1_0));
	zassert_false(pod_frame_is_masked(&frame, PRAHARI_P22_PM2_5));
	zassert_false(pod_frame_is_masked(&frame, PRAHARI_P23_PM10));
	zassert_equal(frame.value[PRAHARI_P22_PM2_5], 36);
	zassert_false(frame.flags[PRAHARI_P22_PM2_5] & XDCR_F_STALE);
	zassert_true(be_state.on[D_PM], "the reader does not close the window");

	/* Window closes at 30 s. */
	fake_now += 5000;
	pwr_tick(&pwr, fake_now);
	zassert_false(be_state.on[D_PM]);

	/* Next cadence cycle carries the value, marked stale, still valid. */
	zassert_equal(sampler_cycle(&smp), 0);
	zassert_equal(f_pm.reads, 1);
	zassert_false(pod_frame_is_masked(&frame, PRAHARI_P22_PM2_5));
	zassert_true(frame.flags[PRAHARI_P22_PM2_5] & XDCR_F_STALE);
	zassert_equal(frame.value[PRAHARI_P22_PM2_5], 36);
}

ZTEST(sampler, test_mox_masked_until_armed_and_warm)
{
	const struct xdcr_desc *const sel[] = {&x_mox};

	start(sel, 1);
	zassert_equal(sampler_cycle(&smp), 1);
	zassert_equal(f_mox.reads, 0, "unarmed heater: no read attempted");
	zassert_true(pod_frame_is_masked(&frame, PRAHARI_P31_TVOC));
	zassert_true(frame.flags[PRAHARI_P31_TVOC] & XDCR_F_NOPOWER);
	zassert_false(be_state.on[D_MOX]);

	zassert_ok(pwr_arm(&pwr, D_MOX, true, fake_now));
	zassert_equal(sampler_cycle(&smp), 0);
	zassert_equal(f_mox.reads, 1);
	zassert_true(f_mox.settled_at_read, "waited the heater warm-up");
	zassert_equal(total_slept, 30000);
	zassert_false(pod_frame_is_masked(&frame, PRAHARI_P31_TVOC));
	zassert_true(be_state.on[D_MOX], "armed rail stays up after the read");

	zassert_ok(pwr_arm(&pwr, D_MOX, false, fake_now));
	zassert_true(pwr_all_off(&pwr));
}

ZTEST(sampler, test_shared_rail_across_two_transducers)
{
	/* T/RH and soil on different rails; T/RH twice on one rail via refs. */
	const struct xdcr_desc *const sel[] = {&x_trh, &x_soil, &x_rain};

	start(sel, 3);
	zassert_equal(sampler_cycle(&smp), 0);
	zassert_true(pwr_all_off(&pwr));
	zassert_equal(total_slept, 20 + 100);
	zassert_false(pod_frame_is_masked(&frame, PRAHARI_P13_AIR_TEMP_UNDERSTORY));
	zassert_false(pod_frame_is_masked(&frame, PRAHARI_P15_RH_UNDERSTORY));
	zassert_equal(frame.value[PRAHARI_P15_RH_UNDERSTORY], 25001);
	zassert_equal(frame.cycle, 1);

	/* Nothing from an unselected transducer ever appears. */
	zassert_true(pod_frame_is_masked(&frame, PRAHARI_P22_PM2_5));
	zassert_true(pod_frame_is_masked(&frame, PRAHARI_P31_TVOC));
}

/* --- pulse counter (class D) --- */

ZTEST(sampler, test_pulse_debounce_and_take)
{
	struct pulse_counter pc;

	pulse_counter_init(&pc, 10);
	zassert_true(pulse_counter_edge(&pc, 100));
	zassert_false(pulse_counter_edge(&pc, 105), "bounce inside 10 ms");
	zassert_false(pulse_counter_edge(&pc, 109));
	zassert_true(pulse_counter_edge(&pc, 110));
	zassert_true(pulse_counter_edge(&pc, 500));
	zassert_equal(pulse_counter_total(&pc), 3);

	zassert_equal(pulse_counter_take(&pc), 3);
	zassert_equal(pulse_counter_take(&pc), 0);
	zassert_true(pulse_counter_edge(&pc, 600));
	zassert_equal(pulse_counter_take(&pc), 1);
	zassert_equal(pulse_counter_total(&pc), 4, "total is cumulative (P12 semantics)");
}

ZTEST(sampler, test_pulse_first_edge_and_wrap)
{
	struct pulse_counter pc;

	pulse_counter_init(&pc, 10);
	zassert_true(pulse_counter_edge(&pc, 0), "first edge at t=0 counts");
	zassert_true(pulse_counter_edge(&pc, UINT32_MAX - 2));
	zassert_false(pulse_counter_edge(&pc, 3), "5 ms across the wrap is a bounce");
	zassert_true(pulse_counter_edge(&pc, 8));
	zassert_equal(pulse_counter_total(&pc), 3);
}
