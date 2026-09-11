/*
 * Blueprint §4.3 pipeline: calibration, median-of-5 vs the anomaly gate,
 * range gating that flags but keeps, and wind u/v continuity across 0°.
 */
#include <math.h>
#include <string.h>
#include <zephyr/ztest.h>
#include <prahari/derive.h>
#include <prahari/pipeline.h>

/* ---- fixtures: a U pod with air T/RH (engineering units) and a soil
 * probe (raw mV) and wind ---- */
static const uint8_t p_trh[] = {PRAHARI_P13_AIR_TEMP_UNDERSTORY, PRAHARI_P15_RH_UNDERSTORY};
static const uint8_t p_soil[] = {PRAHARI_P1_SOIL_VWC_10CM};
static const uint8_t p_wind[] = {PRAHARI_P18_WIND_SPEED, PRAHARI_P19_WIND_DIRECTION};
static const struct xdcr_desc x_trh = {.name = "trh", .position_mask = 0xFF, .domain = -1,
				       .n_params = 2, .params = p_trh};
static const struct xdcr_desc x_soil = {.name = "soil", .position_mask = 0xFF, .domain = -1,
					.n_params = 1, .params = p_soil};
static const struct xdcr_desc x_wind = {.name = "wind", .position_mask = 0xFF, .domain = -1,
					.n_params = 2, .params = p_wind};
static const struct xdcr_desc *const sel[] = {&x_trh, &x_soil, &x_wind};

static const struct pod_range ranges[] = {
	{PRAHARI_P13_AIR_TEMP_UNDERSTORY, -40000, 60000},
	{PRAHARI_P15_RH_UNDERSTORY, 0, 100000},
	{PRAHARI_P1_SOIL_VWC_10CM, 0, 1000},
};

/* Soil probe: 500 mV -> 0.000 m3/m3, 2500 mV -> 0.500 m3/m3 */
static struct calib_table calib = {
	.e = {{PRAHARI_P1_SOIL_VWC_10CM, 500, 0, 2500, 500}},
	.n = 1,
};

static struct pipeline pl;
static struct pod_frame in;
static struct pod_report rep;

static void frame_set(uint8_t p, int32_t v, uint8_t flags)
{
	in.value[p] = v;
	in.flags[p] = XDCR_F_VALID | flags;
}

static void start(void)
{
	struct pipeline_cfg cfg = {
		.calib = &calib,
		.ranges = ranges,
		.n_ranges = ARRAY_SIZE(ranges),
		.z_thr_centi = 300,
		.z_floor_permille = 10,
	};

	memset(&in, 0, sizeof(in));
	zassert_ok(pipeline_init(&pl, &cfg, sel, ARRAY_SIZE(sel)));
}

static void run(void)
{
	in.cycle++;
	pipeline_run(&pl, &in, sel, ARRAY_SIZE(sel), 3300, true, 0, &rep);
}

ZTEST_SUITE(pipeline, NULL, NULL, NULL, NULL, NULL);

/* ---------------- calibration ---------------- */

ZTEST(pipeline, test_calibration_two_point_exact)
{
	const struct calib_entry e = {PRAHARI_P1_SOIL_VWC_10CM, 500, 0, 2500, 500};
	int32_t out;

	zassert_ok(calib_apply(&e, 500, &out));
	zassert_equal(out, 0, "low reference maps exactly");
	zassert_ok(calib_apply(&e, 2500, &out));
	zassert_equal(out, 500, "high reference maps exactly");
	zassert_ok(calib_apply(&e, 1500, &out));
	zassert_equal(out, 250, "linear between the points");
	zassert_ok(calib_apply(&e, 100, &out));
	zassert_equal(out, -100, "extrapolates below (flagged later by range gate)");
	zassert_ok(calib_apply(&e, 1503, &out));
	zassert_equal(out, 251, "rounds to nearest (250.75 -> 251)");
}

ZTEST(pipeline, test_calibration_inverted_and_degenerate)
{
	/* Falling response (e.g. NTC) and offset-only channels work; a
	 * zero raw span is rejected rather than dividing by zero. */
	const struct calib_entry inv = {PRAHARI_P4_SOIL_TEMP_10CM, 3000, -10000, 1000, 50000};
	const struct calib_entry bad = {PRAHARI_P4_SOIL_TEMP_10CM, 1000, 0, 1000, 1};
	int32_t out;

	zassert_ok(calib_apply(&inv, 2000, &out));
	zassert_equal(out, 20000);
	zassert_equal(calib_apply(&bad, 1000, &out), -EINVAL);
}

ZTEST(pipeline, test_calibration_eeprom_roundtrip_and_corruption)
{
	uint8_t blob[CALIB_BLOB_MAX];
	struct calib_table t2, src = {
		.e = {{PRAHARI_P1_SOIL_VWC_10CM, 500, 0, 2500, 500},
		      {PRAHARI_P13_AIR_TEMP_UNDERSTORY, -40000, -40250, 60000, 59800}},
		.n = 2,
	};
	int len = calib_table_encode(&src, blob, sizeof(blob));

	zassert_equal(len, CALIB_HDR_LEN + 2 * CALIB_ENTRY_LEN + 1);
	zassert_ok(calib_table_decode(blob, len, &t2));
	zassert_equal(t2.n, 2);
	zassert_mem_equal(t2.e, src.e, 2 * sizeof(struct calib_entry));
	zassert_equal(calib_find(&t2, PRAHARI_P13_AIR_TEMP_UNDERSTORY)->ref_hi, 59800);
	zassert_is_null(calib_find(&t2, PRAHARI_P15_RH_UNDERSTORY));

	blob[7] ^= 0x01; /* flip a coefficient bit */
	zassert_equal(calib_table_decode(blob, len, &t2), -EINVAL, "CRC must catch it");
	zassert_equal(t2.n, 0);

	memset(blob, 0xFF, sizeof(blob));
	zassert_equal(calib_table_decode(blob, sizeof(blob), &t2), -ENOENT, "blank EEPROM");
	zassert_equal(calib_table_decode(NULL, 0, &t2), -ENOENT);
}

ZTEST(pipeline, test_pipeline_applies_calibration_and_marks_uncalibrated)
{
	start();
	frame_set(PRAHARI_P1_SOIL_VWC_10CM, 1500, XDCR_F_RAW);
	frame_set(PRAHARI_P13_AIR_TEMP_UNDERSTORY, 20000, 0);
	frame_set(PRAHARI_P15_RH_UNDERSTORY, 50000, 0);
	frame_set(PRAHARI_P18_WIND_SPEED, 3, XDCR_F_RAW); /* pulses, no cal entry */
	frame_set(PRAHARI_P19_WIND_DIRECTION, 1800, XDCR_F_RAW);
	run();

	zassert_equal(rep.primary.value[PRAHARI_P1_SOIL_VWC_10CM], 250, "mV -> m3/m3 milli");
	zassert_false(rep.primary.flags[PRAHARI_P1_SOIL_VWC_10CM] & XDCR_F_RAW);
	zassert_false(rep.primary.flags[PRAHARI_P1_SOIL_VWC_10CM] & XDCR_F_UNCAL);

	zassert_equal(rep.primary.value[PRAHARI_P13_AIR_TEMP_UNDERSTORY], 20000,
		      "engineering units pass through identity");

	zassert_true(rep.primary.flags[PRAHARI_P18_WIND_SPEED] & XDCR_F_UNCAL,
		     "raw pulses with no entry: kept, marked unusable");
	zassert_equal(rep.primary.value[PRAHARI_P18_WIND_SPEED], 3, "value retained");
	zassert_false(rep.derived_flags[DER_S15_WIND_U] & XDCR_F_VALID,
		      "no wind vector from uncalibrated inputs");
	zassert_true(rep.derived_flags[DER_S13_VPD] & XDCR_F_VALID);
	zassert_equal(rep.self.rail_mv, 3300);
	zassert_true(rep.self.rail_valid);
}

/* ---------------- median vs anomaly gate ---------------- */

static void feed_temp(int32_t t_mc, int cycles)
{
	for (int i = 0; i < cycles; i++) {
		frame_set(PRAHARI_P13_AIR_TEMP_UNDERSTORY, t_mc, 0);
		frame_set(PRAHARI_P15_RH_UNDERSTORY, 50000, 0);
		run();
	}
}

ZTEST(pipeline, test_single_spike_rejected_by_median_no_wake)
{
	start();
	feed_temp(20000, ZGATE_WINDOW + 2); /* settle: window full, flat */
	zassert_false(rep.wake);

	feed_temp(60000, 1); /* one-sample glitch, +40 degC */
	zassert_equal(rep.primary.value[PRAHARI_P13_AIR_TEMP_UNDERSTORY], 20000,
		      "median-of-5 removes a single spike");
	zassert_false(rep.primary.flags[PRAHARI_P13_AIR_TEMP_UNDERSTORY] & XDCR_F_ANOMALY);
	zassert_false(rep.wake, "spike never reaches the anomaly gate");
	zassert_within(rep.derived[DER_S13_VPD], 1169, 5, "derivations see the filtered value");

	feed_temp(20000, 4); /* spike ages out of the window without effect */
	zassert_false(rep.wake);
	zassert_equal(rep.primary.value[PRAHARI_P13_AIR_TEMP_UNDERSTORY], 20000);
}

ZTEST(pipeline, test_sustained_deviation_passes_median_and_trips_gate)
{
	int32_t v;

	start();
	feed_temp(20000, ZGATE_WINDOW + 2);
	zassert_false(rep.wake);

	/* Two samples of a real step: still outvoted by the median. */
	feed_temp(40000, 2);
	v = rep.primary.value[PRAHARI_P13_AIR_TEMP_UNDERSTORY];
	zassert_equal(v, 20000, "2 of 5 is still a minority");
	zassert_false(rep.wake);

	/* Third consecutive sample: the median flips, the gate must fire. */
	feed_temp(40000, 1);
	v = rep.primary.value[PRAHARI_P13_AIR_TEMP_UNDERSTORY];
	zassert_equal(v, 40000, "3 of 5 carries the median");
	zassert_true(rep.primary.flags[PRAHARI_P13_AIR_TEMP_UNDERSTORY] & XDCR_F_ANOMALY);
	zassert_true(rep.wake, "sustained deviation raises the wake flag");
	zassert_true(rep.wake_params & ((uint64_t)1 << PRAHARI_P13_AIR_TEMP_UNDERSTORY));
	zassert_false(rep.wake_params & ((uint64_t)1 << PRAHARI_P15_RH_UNDERSTORY),
		      "a steady channel is not flagged");
	zassert_true(rep.derived_flags[DER_S13_VPD] & XDCR_F_ANOMALY,
		     "derived values inherit the anomaly flag");

	/* Once the new level dominates the window it is the new normal. */
	feed_temp(40000, ZGATE_WINDOW);
	zassert_false(rep.wake, "gate re-baselines on its short window");
}

ZTEST(pipeline, test_zgate_flat_signal_floor_and_history_bound)
{
	struct zgate g;
	int32_t z;

	zgate_init(&g);
	for (int i = 0; i < ZGATE_WINDOW; i++) {
		zassert_equal(zgate_update(&g, 1000, 300, 10, &z), -EAGAIN, "filling");
	}
	zassert_true(zgate_full(&g));
	zassert_equal(zgate_update(&g, 1000, 300, 10, &z), 0);
	zassert_equal(z, 0);
	/* std floor = 1 % of mean = 10 -> a 5 % step is z = 5.0 */
	zassert_equal(zgate_update(&g, 1050, 300, 10, &z), 1);
	zassert_equal(z, 500);
	/* Window length bounds all history the pod holds: ZGATE_WINDOW samples. */
	zassert_true(ZGATE_WINDOW <= 32, "history stays a short window, never days");
}

/* ---------------- range gating ---------------- */

ZTEST(pipeline, test_range_gated_value_flagged_but_returned)
{
	start();
	frame_set(PRAHARI_P1_SOIL_VWC_10CM, 100, XDCR_F_RAW); /* -> -100 milli m3/m3 */
	frame_set(PRAHARI_P13_AIR_TEMP_UNDERSTORY, 75000, 0);  /* 75 degC, above 60 */
	frame_set(PRAHARI_P15_RH_UNDERSTORY, 50000, 0);
	run();

	zassert_true(rep.primary.flags[PRAHARI_P1_SOIL_VWC_10CM] & XDCR_F_VALID);
	zassert_true(rep.primary.flags[PRAHARI_P1_SOIL_VWC_10CM] & XDCR_F_RANGE);
	zassert_equal(rep.primary.value[PRAHARI_P1_SOIL_VWC_10CM], -100, "value kept");

	zassert_true(rep.primary.flags[PRAHARI_P13_AIR_TEMP_UNDERSTORY] & XDCR_F_RANGE);
	zassert_equal(rep.primary.value[PRAHARI_P13_AIR_TEMP_UNDERSTORY], 75000);
	zassert_true(rep.primary.flags[PRAHARI_P13_AIR_TEMP_UNDERSTORY] & XDCR_F_VALID,
		     "flagged, not discarded");
	zassert_false(rep.primary.flags[PRAHARI_P15_RH_UNDERSTORY] & XDCR_F_RANGE);

	zassert_true(rep.derived_flags[DER_S13_VPD] & XDCR_F_VALID, "still derived");
	zassert_true(rep.derived_flags[DER_S13_VPD] & XDCR_F_RANGE, "but carries the flag");
	zassert_equal(rep.self.xdcr_range, BIT(0) | BIT(1), "self-report: trh and soil");
}

ZTEST(pipeline, test_masked_and_faulted_channels_stay_masked)
{
	start();
	in.flags[PRAHARI_P13_AIR_TEMP_UNDERSTORY] = XDCR_F_FAULT; /* read failed */
	frame_set(PRAHARI_P15_RH_UNDERSTORY, 50000, 0);
	pipeline_run(&pl, &in, sel, ARRAY_SIZE(sel), 0, false, BIT(2), &rep);

	zassert_false(rep.primary.flags[PRAHARI_P13_AIR_TEMP_UNDERSTORY] & XDCR_F_VALID);
	zassert_true(rep.primary.flags[PRAHARI_P13_AIR_TEMP_UNDERSTORY] & XDCR_F_FAULT);
	zassert_false(rep.derived_flags[DER_S13_VPD] & XDCR_F_VALID, "no VPD without T");
	zassert_false(rep.derived_flags[DER_S21_HEAT_INDEX] & XDCR_F_VALID);
	zassert_equal(rep.self.xdcr_fault, BIT(0) | BIT(2), "read fault + init fault");
	zassert_false(rep.self.rail_valid);
}

/* ---------------- derivations ---------------- */

ZTEST(pipeline, test_vpd_dewpoint_heat_index_reference_values)
{
	int32_t vpd, td, hi;

	/* 20 degC, 50 %: es = 2.338 kPa, VPD = 1.169 kPa, Td = 9.26 degC */
	zassert_ok(derive_vpd_dewpoint(20000, 50000, &vpd, &td));
	zassert_within(vpd, 1169, 5);
	zassert_within(td, 9260, 30);
	/* saturated air: VPD 0, dew point = T */
	zassert_ok(derive_vpd_dewpoint(25000, 100000, &vpd, &td));
	zassert_within(vpd, 0, 1);
	zassert_within(td, 25000, 5);
	/* 32 degC, 70 %: NWS heat index ~ 41 degC */
	zassert_ok(derive_heat_index(32000, 70000, &hi));
	zassert_within(hi, 41000, 700);
	/* cool: Steadman form, HI below T */
	zassert_ok(derive_heat_index(20000, 50000, &hi));
	zassert_within(hi, 19000, 1500);
}

ZTEST(pipeline, test_wind_vector_no_discontinuity_at_north)
{
	int32_t u1, v1, u2, v2, s1, c1, s2, c2;

	/* 359.9 deg vs 0.1 deg at 10 m/s: a 0.2 deg step, so u/v must
	 * differ by ~0.035 m/s, not by anything resembling a wrap. */
	zassert_ok(derive_wind_uv(10000, 359900, &u1, &v1, &s1, &c1));
	zassert_ok(derive_wind_uv(10000, 100, &u2, &v2, &s2, &c2));
	zassert_within(u1, u2, 40, "u continuous across 360->0");
	zassert_within(v1, v2, 5, "v continuous across 360->0");
	zassert_within(s1, s2, 5);
	zassert_within(c1, c2, 1);
	zassert_within(v1, -10000, 5, "north wind: v = -speed");

	/* 359 -> 0 exactly, and the largest 1-degree step anywhere on the
	 * circle is bounded by the same chord length (2 sin 0.5 deg = 0.01745). */
	zassert_ok(derive_wind_uv(10000, 359000, &u1, &v1, NULL, NULL));
	zassert_ok(derive_wind_uv(10000, 0, &u2, &v2, NULL, NULL));
	zassert_within(u1, u2, 176);
	zassert_within(v1, v2, 5);
	for (int d = 0; d < 360; d++) {
		int32_t ua, va, ub, vb;
		int64_t du, dv;

		zassert_ok(derive_wind_uv(10000, d * 1000, &ua, &va, NULL, NULL));
		zassert_ok(derive_wind_uv(10000, ((d + 1) % 360) * 1000, &ub, &vb, NULL, NULL));
		du = ua - ub;
		dv = va - vb;
		zassert_true(du * du + dv * dv <= 176 * 176, "jump at %d deg", d);
	}
}

ZTEST(pipeline, test_wind_vector_cardinal_directions_and_calm)
{
	int32_t u, v;

	zassert_ok(derive_wind_uv(5000, 90000, &u, &v, NULL, NULL)); /* from east */
	zassert_within(u, -5000, 5);
	zassert_within(v, 0, 5);
	zassert_ok(derive_wind_uv(5000, 180000, &u, &v, NULL, NULL)); /* from south */
	zassert_within(u, 0, 5);
	zassert_within(v, 5000, 5);
	zassert_ok(derive_wind_uv(5000, 270000, &u, &v, NULL, NULL)); /* from west */
	zassert_within(u, 5000, 5);
	zassert_within(v, 0, 5);
	zassert_ok(derive_wind_uv(0, 123456, &u, &v, NULL, NULL)); /* calm: bearing irrelevant */
	zassert_equal(u, 0);
	zassert_equal(v, 0);
	zassert_equal(derive_wind_uv(-1, 0, &u, &v, NULL, NULL), -EINVAL);
}

ZTEST(pipeline, test_median5_filling_behaviour)
{
	struct median5 m;

	median5_init(&m);
	zassert_equal(median5_push(&m, 10), 10);
	zassert_equal(median5_push(&m, 100), 10, "even count: lower middle");
	zassert_equal(median5_push(&m, 12), 12);
	zassert_equal(median5_push(&m, 11), 11);
	zassert_equal(median5_push(&m, 13), 12, "full: true median of 5");
	zassert_equal(median5_push(&m, 14), 13, "oldest (10) dropped");
	zassert_equal(median5_count(&m), 5);
}
