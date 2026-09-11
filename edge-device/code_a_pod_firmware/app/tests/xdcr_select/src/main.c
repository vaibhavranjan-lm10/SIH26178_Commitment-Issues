/*
 * Position -> transducer-set selection.  The table mirrors the populations
 * requested for S1/G/U/C (blueprint §11.4 + §5.3) as declared in
 * boards/nucleo_l031k6.overlay; the firmware's real table is generated
 * from that overlay, this test pins the selection semantics.
 */
#include <string.h>
#include <zephyr/ztest.h>
#include <prahari/xdcr.h>

#define M(...) (__VA_ARGS__)
#define D(nm, cl, mask, dom)                                                                      \
	{.name = nm, .cls = cl, .position_mask = mask, .domain = dom, .n_params = 0}

static const struct xdcr_desc table[] = {
	D("soil_10cm", XDCR_CLASS_C_ADC, POD_POS_BIT(POD_POS_S1), 1),
	D("soil_40cm", XDCR_CLASS_C_ADC, POD_POS_BIT(POD_POS_S2), 1),
	D("soil_100cm", XDCR_CLASS_C_ADC, POD_POS_BIT(POD_POS_S3), 1),
	D("surface_soil", XDCR_CLASS_C_ADC, POD_POS_BIT(POD_POS_G), 1),
	D("pore_pressure", XDCR_CLASS_C_ADC, POD_POS_BIT(POD_POS_G), 4),
	D("mox_gas", XDCR_CLASS_C_ADC, POD_POS_BIT(POD_POS_U), 3),
	D("pyranometer", XDCR_CLASS_C_ADC, POD_POS_BIT(POD_POS_C), 4),
	D("wind_vane", XDCR_CLASS_C_ADC, POD_POS_BIT(POD_POS_U) | POD_POS_BIT(POD_POS_C), 4),
	D("rain_gauge", XDCR_CLASS_D_PULSE, POD_POS_BIT(POD_POS_G), XDCR_DOMAIN_NONE),
	D("anemometer", XDCR_CLASS_D_PULSE, POD_POS_BIT(POD_POS_U) | POD_POS_BIT(POD_POS_C), 0),
	D("air_trh_understory", XDCR_CLASS_A_I2C, POD_POS_BIT(POD_POS_U), 0),
	D("air_trh_canopy", XDCR_CLASS_A_I2C, POD_POS_BIT(POD_POS_C), 0),
	D("barometer", XDCR_CLASS_A_I2C, POD_POS_BIT(POD_POS_U), 0),
	D("imu_tilt", XDCR_CLASS_A_I2C, POD_POS_BIT(POD_POS_G), 0),
	D("particulate", XDCR_CLASS_B_UART, POD_POS_BIT(POD_POS_U), 2),
};

static bool has(const struct xdcr_desc **sel, size_t n, const char *name)
{
	for (size_t i = 0; i < n; i++) {
		if (strcmp(sel[i]->name, name) == 0) {
			return true;
		}
	}
	return false;
}

static void expect(enum pod_position pos, const char *const *names, size_t count)
{
	const struct xdcr_desc *sel[ARRAY_SIZE(table)];
	size_t n = xdcr_select(table, ARRAY_SIZE(table), pos, sel, ARRAY_SIZE(sel));

	zassert_equal(n, count, "%s: selected %u, expected %u", pod_position_name(pos),
		      (unsigned)n, (unsigned)count);
	for (size_t i = 0; i < count; i++) {
		zassert_true(has(sel, n, names[i]), "%s missing %s", pod_position_name(pos),
			     names[i]);
	}
}

ZTEST_SUITE(xdcr_select, NULL, NULL, NULL, NULL, NULL);

ZTEST(xdcr_select, test_s1_soil_probe_only)
{
	const char *const names[] = {"soil_10cm"};

	expect(POD_POS_S1, names, ARRAY_SIZE(names));
}

ZTEST(xdcr_select, test_s2_s3_each_own_depth)
{
	const char *const s2[] = {"soil_40cm"};
	const char *const s3[] = {"soil_100cm"};

	expect(POD_POS_S2, s2, 1);
	expect(POD_POS_S3, s3, 1);
}

ZTEST(xdcr_select, test_g_ground_population)
{
	const char *const names[] = {"rain_gauge", "surface_soil", "imu_tilt", "pore_pressure"};

	expect(POD_POS_G, names, ARRAY_SIZE(names));
}

ZTEST(xdcr_select, test_u_understory_population)
{
	const char *const names[] = {"air_trh_understory", "barometer", "particulate",
				     "mox_gas", "anemometer", "wind_vane"};

	expect(POD_POS_U, names, ARRAY_SIZE(names));
}

ZTEST(xdcr_select, test_c_canopy_population)
{
	const char *const names[] = {"air_trh_canopy", "anemometer", "wind_vane", "pyranometer"};

	expect(POD_POS_C, names, ARRAY_SIZE(names));
}

ZTEST(xdcr_select, test_same_part_different_params_by_position)
{
	/* One SHT4x, but U reports P13/P15 and C reports P14/P16: never both. */
	const struct xdcr_desc *sel[ARRAY_SIZE(table)];
	size_t n;

	n = xdcr_select(table, ARRAY_SIZE(table), POD_POS_U, sel, ARRAY_SIZE(sel));
	zassert_true(has(sel, n, "air_trh_understory"));
	zassert_false(has(sel, n, "air_trh_canopy"));
	n = xdcr_select(table, ARRAY_SIZE(table), POD_POS_C, sel, ARRAY_SIZE(sel));
	zassert_false(has(sel, n, "air_trh_understory"));
	zassert_true(has(sel, n, "air_trh_canopy"));
}

ZTEST(xdcr_select, test_only_rain_gauge_is_passive)
{
	for (size_t i = 0; i < ARRAY_SIZE(table); i++) {
		bool passive = table[i].domain == XDCR_DOMAIN_NONE;

		zassert_equal(passive, strcmp(table[i].name, "rain_gauge") == 0,
			      "%s: §3.4 says only the rain gauge stays unswitched",
			      table[i].name);
	}
}

ZTEST(xdcr_select, test_every_transducer_has_a_position)
{
	for (size_t i = 0; i < ARRAY_SIZE(table); i++) {
		zassert_not_equal(table[i].position_mask, 0, "%s orphaned", table[i].name);
		zassert_equal(table[i].position_mask >> POD_POS_COUNT, 0, "%s bad mask",
			      table[i].name);
	}
}

ZTEST(xdcr_select, test_invalid_position_and_truncation)
{
	const struct xdcr_desc *sel[2];
	size_t n;

	n = xdcr_select(table, ARRAY_SIZE(table), POD_POS_COUNT, sel, 2);
	zassert_equal(n, 0);
	n = xdcr_select(table, ARRAY_SIZE(table), POD_POS_U, sel, 2);
	zassert_equal(n, 6, "count reports the full population even when out is short");
}
