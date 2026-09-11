/*
 * Switched-rail power controller against the blueprint §3.4 duty table:
 *   particulate fan   ~80 mA   30 s every 15 min
 *   gas MOX heater    ~40 mA   on only when armed
 *   soil excitation   ~10 mA   200 ms per read
 *   ultrasonic level  ~50 mA   100 ms per read
 *   air T/RH          <1 mA    continuous acceptable
 * Nothing energised between reads; the rain gauge has no rail at all.
 */
#include <zephyr/ztest.h>
#include <prahari/power.h>

enum { D_PM, D_MOX, D_SOIL, D_US, D_TRH, D_N };

#define MIN15 (15u * 60u * 1000u)

static const struct pwr_domain_cfg cfg[D_N] = {
	[D_PM] = {"particulate-fan", PWR_POLICY_PERIODIC, 80, 25000, 30000, MIN15},
	[D_MOX] = {"mox-heater", PWR_POLICY_ARMED, 40, 30000, 0, 0},
	[D_SOIL] = {"soil-excitation", PWR_POLICY_PER_READ, 10, 100, 200, 0},
	[D_US] = {"ultrasonic", PWR_POLICY_PER_READ, 50, 50, 100, 0},
	[D_TRH] = {"air-trh", PWR_POLICY_HOLD, 1, 20, 0, 0},
};

/* Fake load-switch backend: records the rail states and every call. */
static struct {
	bool on[D_N];
	int calls;
	int fail_dom; /* -1: never fail */
} be_state;

static int be_set(void *ctx, uint8_t dom, bool on)
{
	ARG_UNUSED(ctx);
	be_state.calls++;
	if ((int)dom == be_state.fail_dom && on) {
		return -EIO;
	}
	be_state.on[dom] = on;
	return 0;
}

static const struct pwr_backend be = {.set = be_set};

/* Event recorder. */
static struct {
	int count[D_N][4];
} ev;

static void on_event(void *ctx, uint8_t dom, enum pwr_event e)
{
	ARG_UNUSED(ctx);
	ev.count[dom][e]++;
}

static struct pwr_domain_state st[D_N];
static struct pwr_ctl c;

static void setup_budget(uint16_t budget, uint32_t now)
{
	memset(&be_state, 0, sizeof(be_state));
	be_state.fail_dom = -1;
	memset(&ev, 0, sizeof(ev));
	zassert_ok(pwr_init(&c, cfg, st, D_N, &be, budget, now));
	pwr_set_event_cb(&c, on_event, NULL);
}

static void before(void *f)
{
	ARG_UNUSED(f);
	setup_budget(0, 0);
}

ZTEST_SUITE(power, NULL, NULL, before, NULL, NULL);

ZTEST(power, test_init_everything_off)
{
	for (int d = 0; d < D_N; d++) {
		zassert_false(be_state.on[d], "%s energised at init", cfg[d].name);
		zassert_equal(pwr_state(&c, d), PWR_STATE_OFF);
	}
	zassert_equal(be_state.calls, D_N, "every rail driven off explicitly at init");
	zassert_true(pwr_all_off(&c));
	zassert_equal(pwr_load_ma(&c), 0);
}

ZTEST(power, test_per_read_soil_excitation_lifecycle)
{
	/* §3.4: 10 mA, 200 ms per read: on -> settle -> read -> off. */
	zassert_ok(pwr_acquire(&c, D_SOIL, 1000));
	zassert_true(be_state.on[D_SOIL]);
	zassert_equal(pwr_state(&c, D_SOIL), PWR_STATE_SETTLING);
	zassert_false(pwr_is_ready(&c, D_SOIL, 1050));
	zassert_equal(pwr_settle_remaining_ms(&c, D_SOIL, 1050), 50);
	zassert_true(pwr_is_ready(&c, D_SOIL, 1100));
	zassert_equal(pwr_load_ma(&c), 10);

	pwr_tick(&c, 1100);
	zassert_equal(pwr_state(&c, D_SOIL), PWR_STATE_ON);
	zassert_equal(ev.count[D_SOIL][PWR_EVT_READY], 1);

	zassert_ok(pwr_release(&c, D_SOIL, 1150));
	zassert_false(be_state.on[D_SOIL], "per-read rail must drop at release");
	zassert_equal(ev.count[D_SOIL][PWR_EVT_OFF], 1);
	zassert_equal(pwr_state(&c, D_SOIL), PWR_STATE_OFF);
	/* The tick above also opened the fan's first duty window (80 mA);
	 * that is the only thing left energised. */
	zassert_equal(pwr_load_ma(&c), cfg[D_PM].current_ma, "nothing else energised");
	pwr_tick(&c, 1100 + 30000);
	zassert_true(pwr_all_off(&c), "nothing energised between reads");
}

ZTEST(power, test_per_read_refcount)
{
	zassert_ok(pwr_acquire(&c, D_SOIL, 0));
	zassert_ok(pwr_acquire(&c, D_SOIL, 0));
	zassert_ok(pwr_release(&c, D_SOIL, 100));
	zassert_true(be_state.on[D_SOIL], "still referenced");
	zassert_ok(pwr_release(&c, D_SOIL, 100));
	zassert_false(be_state.on[D_SOIL]);
	zassert_equal(pwr_release(&c, D_SOIL, 100), -EINVAL, "release without acquire");
}

ZTEST(power, test_per_read_on_time_cap_forces_off)
{
	/* A driver that never releases must not leave the 200 ms rail up. */
	zassert_ok(pwr_acquire(&c, D_SOIL, 0));
	pwr_tick(&c, 199);
	zassert_true(be_state.on[D_SOIL]);
	pwr_tick(&c, 200);
	zassert_false(be_state.on[D_SOIL]);
	zassert_equal(ev.count[D_SOIL][PWR_EVT_TIMEOUT], 1);
	zassert_equal(pwr_release(&c, D_SOIL, 201), -EINVAL, "refs were cleared");
}

ZTEST(power, test_periodic_fan_30s_every_15min)
{
	/* First window opens at the first tick after boot. */
	zassert_false(be_state.on[D_PM]);
	pwr_tick(&c, 0);
	zassert_true(be_state.on[D_PM]);
	zassert_equal(pwr_load_ma(&c), 80);

	/* Not readable until settled; acquire allowed while window open. */
	zassert_false(pwr_is_ready(&c, D_PM, 24999));
	pwr_tick(&c, 24999);
	zassert_equal(ev.count[D_PM][PWR_EVT_READY], 0);
	pwr_tick(&c, 25000);
	zassert_equal(ev.count[D_PM][PWR_EVT_READY], 1);
	zassert_ok(pwr_acquire(&c, D_PM, 25000));
	zassert_ok(pwr_release(&c, D_PM, 25100));
	zassert_true(be_state.on[D_PM], "periodic rail is owned by the window, not the reader");

	/* Window closes at 30 s. */
	pwr_tick(&c, 29999);
	zassert_true(be_state.on[D_PM]);
	pwr_tick(&c, 30000);
	zassert_false(be_state.on[D_PM]);
	zassert_equal(ev.count[D_PM][PWR_EVT_OFF], 1);
	zassert_equal(pwr_acquire(&c, D_PM, 30001), -EAGAIN, "no read outside the window");

	/* Stays off until 15 min after the window opened, then repeats. */
	for (uint32_t t = 31000; t < MIN15; t += 1000) {
		pwr_tick(&c, t);
		zassert_false(be_state.on[D_PM], "fan on at t=%u", t);
	}
	pwr_tick(&c, MIN15);
	zassert_true(be_state.on[D_PM]);
	pwr_tick(&c, MIN15 + 30000);
	zassert_false(be_state.on[D_PM]);
	zassert_equal(ev.count[D_PM][PWR_EVT_OFF], 2);
}

ZTEST(power, test_mox_heater_only_when_armed)
{
	zassert_equal(pwr_acquire(&c, D_MOX, 0), -EACCES, "unarmed: no read, no rail");
	zassert_false(be_state.on[D_MOX]);

	zassert_ok(pwr_arm(&c, D_MOX, true, 1000));
	zassert_true(be_state.on[D_MOX]);
	zassert_equal(pwr_load_ma(&c), 40);
	zassert_false(pwr_is_ready(&c, D_MOX, 1000 + 29999), "heater warm-up");
	zassert_true(pwr_is_ready(&c, D_MOX, 1000 + 30000));
	zassert_ok(pwr_acquire(&c, D_MOX, 31000));
	zassert_ok(pwr_release(&c, D_MOX, 31100));
	zassert_true(be_state.on[D_MOX], "armed rail stays up between reads");

	/* Long armed periods are not capped (on_ms = 0). */
	pwr_tick(&c, 1000 + 3600000);
	zassert_true(be_state.on[D_MOX]);

	zassert_ok(pwr_arm(&c, D_MOX, false, 3700000));
	zassert_false(be_state.on[D_MOX]);
	zassert_equal(pwr_acquire(&c, D_MOX, 3700001), -EACCES);
	zassert_equal(pwr_arm(&c, D_SOIL, true, 0), -ENOTSUP, "arming is MOX-only");
}

ZTEST(power, test_hold_policy_stays_on)
{
	zassert_ok(pwr_acquire(&c, D_TRH, 0));
	zassert_ok(pwr_release(&c, D_TRH, 50));
	zassert_true(be_state.on[D_TRH]);
	zassert_ok(pwr_acquire(&c, D_TRH, 60));
	zassert_equal(pwr_state(&c, D_TRH), PWR_STATE_SETTLING, "no re-switch, still settling");
	pwr_tick(&c, 100);
	zassert_true(pwr_is_ready(&c, D_TRH, 100));
}

ZTEST(power, test_rail_budget_refuses_and_defers)
{
	setup_budget(100, 0);

	pwr_tick(&c, 0); /* fan window: 80 mA */
	zassert_true(be_state.on[D_PM]);
	zassert_equal(pwr_arm(&c, D_MOX, true, 0), -EBUSY, "80+40 > 100");
	zassert_false(be_state.on[D_MOX]);
	zassert_ok(pwr_acquire(&c, D_SOIL, 0), "80+10 fits");
	zassert_equal(pwr_acquire(&c, D_US, 0), -EBUSY, "90+50 > 100");
	zassert_equal(pwr_load_ma(&c), 90);
	zassert_ok(pwr_release(&c, D_SOIL, 10));

	/* Periodic window that cannot open is deferred, not dropped. */
	setup_budget(100, 0);
	zassert_ok(pwr_arm(&c, D_MOX, true, 0));
	zassert_ok(pwr_acquire(&c, D_US, 0)); /* 40 + 50 = 90 */
	pwr_tick(&c, 0);
	zassert_false(be_state.on[D_PM]);
	zassert_equal(ev.count[D_PM][PWR_EVT_DEFERRED], 1);
	pwr_tick(&c, 50); /* still inside the ultrasonic 100 ms cap */
	zassert_equal(ev.count[D_PM][PWR_EVT_DEFERRED], 2);
	zassert_ok(pwr_release(&c, D_US, 60));
	zassert_ok(pwr_arm(&c, D_MOX, false, 60));
	pwr_tick(&c, 70);
	zassert_true(be_state.on[D_PM], "window opens once the budget allows");
}

ZTEST(power, test_unbudgeted_current_zero_never_blocks)
{
	static const struct pwr_domain_cfg unspecified[1] = {
		{"analogue-aux", PWR_POLICY_PER_READ, 0, 50, 500, 0}};
	static struct pwr_domain_state s1[1];
	struct pwr_ctl c1;

	memset(&be_state, 0, sizeof(be_state));
	be_state.fail_dom = -1;
	zassert_ok(pwr_init(&c1, unspecified, s1, 1, &be, 1, 0));
	zassert_ok(pwr_acquire(&c1, 0, 0), "0 mA = not specified, must not trip budget");
}

ZTEST(power, test_backend_failure_leaves_domain_off)
{
	be_state.fail_dom = D_SOIL;
	zassert_equal(pwr_acquire(&c, D_SOIL, 0), -EIO);
	zassert_equal(pwr_state(&c, D_SOIL), PWR_STATE_OFF);
	zassert_equal(pwr_release(&c, D_SOIL, 0), -EINVAL);
}

ZTEST(power, test_uptime_wraparound)
{
	uint32_t t0 = UINT32_MAX - 30;

	setup_budget(0, t0);
	zassert_ok(pwr_acquire(&c, D_SOIL, t0));
	zassert_false(pwr_is_ready(&c, D_SOIL, t0 + 99)); /* wraps */
	zassert_true(pwr_is_ready(&c, D_SOIL, t0 + 100));
	pwr_tick(&c, t0 + 199);
	zassert_true(be_state.on[D_SOIL]);
	pwr_tick(&c, t0 + 200);
	zassert_false(be_state.on[D_SOIL], "cap must survive the 32-bit wrap");

	/* Periodic period across the wrap. */
	pwr_tick(&c, t0 + 201);
	zassert_true(be_state.on[D_PM], "first window (was deferred by nothing, opens now)");
}

ZTEST(power, test_bad_domain_index)
{
	zassert_equal(pwr_acquire(&c, D_N, 0), -EINVAL);
	zassert_equal(pwr_release(&c, D_N, 0), -EINVAL);
	zassert_equal(pwr_arm(&c, D_N, true, 0), -EINVAL);
	zassert_false(pwr_is_ready(&c, D_N, 0));
}
