/* GNSS PPS discipline (§6.3): lock, drift estimate, glitch rejection,
 * missed-pulse tolerance, holdover, local->UTC conversion. */
#include <zephyr/ztest.h>
#include <hn/pps.h>

#define S 1000000u

ZTEST_SUITE(pps, NULL, NULL, NULL, NULL, NULL);

ZTEST(pps, test_lock_and_convert)
{
	struct pps_disc d;
	uint64_t utc;

	pps_init(&d, 2000, 3, 10 * S);
	zassert_equal(d.state, PPS_UNLOCKED);
	zassert_equal(pps_local_to_utc_ms(&d, 0, &utc), -EAGAIN);

	zassert_equal(pps_edge(&d, 5 * S), 1);
	pps_set_utc(&d, 1757376000);     /* GNSS: that edge was 00:00:00 */
	zassert_equal(pps_edge(&d, 6 * S), 1);
	zassert_equal(pps_edge(&d, 7 * S), 1);
	zassert_equal(d.state, PPS_UNLOCKED, "needs 3 good intervals");
	zassert_equal(pps_edge(&d, 8 * S), 1);
	zassert_equal(d.state, PPS_LOCKED);
	zassert_equal(d.utc_s_at_edge, 1757376003, "UTC advanced with each pulse");

	zassert_ok(pps_local_to_utc_ms(&d, 8 * S + 250000, &utc));
	zassert_equal(utc, 1757376003250ull);
	zassert_ok(pps_local_to_utc_ms(&d, 8 * S - 100000, &utc));
	zassert_equal(utc, 1757376002900ull, "instants before the last edge work too");
	zassert_equal(pps_drift_ppm(&d), 0);
}

ZTEST(pps, test_drift_estimate_and_scaled_conversion)
{
	/* Local oscillator runs 100 ppm fast: 1 000 100 local us per true second. */
	struct pps_disc d;
	uint64_t utc;
	uint32_t t = 0;

	pps_init(&d, 2000, 3, 0);
	pps_edge(&d, t);
	pps_set_utc(&d, 100);
	for (int i = 0; i < 60; i++) {
		t += S + 100;
		zassert_equal(pps_edge(&d, t), 1);
	}
	zassert_equal(d.state, PPS_LOCKED);
	zassert_within(pps_drift_ppm(&d), 100, 2, "EMA converged to +100 ppm");
	/* Half a (true) second after the edge is 500 050 local us. */
	zassert_ok(pps_local_to_utc_ms(&d, t + 500050, &utc));
	zassert_equal(utc, 160 * 1000 + 500);
}

ZTEST(pps, test_glitch_rejected_missed_pulse_tolerated)
{
	struct pps_disc d;

	pps_init(&d, 2000, 2, 0);
	pps_edge(&d, 10 * S);
	pps_set_utc(&d, 1000);
	pps_edge(&d, 11 * S);
	pps_edge(&d, 12 * S);
	zassert_equal(d.state, PPS_LOCKED);

	zassert_equal(pps_edge(&d, 12 * S + 30000), 0, "30 ms after a pulse: noise");
	zassert_equal(d.glitches, 1);
	zassert_equal(d.utc_s_at_edge, 1002, "glitch did not advance UTC");
	zassert_equal(d.last_edge_us, 12 * S, "nor move the reference edge");

	zassert_equal(pps_edge(&d, 13 * S + 1500), 1, "within tolerance");
	zassert_equal(pps_edge(&d, 16 * S + 1000), 1, "two missed pulses: 3 s gap accepted");
	zassert_equal(d.utc_s_at_edge, 1006, "UTC advanced by the whole seconds elapsed");
	zassert_equal(pps_edge(&d, 16 * S + 500000), 0, "half a second: glitch");
	zassert_equal(pps_edge(&d, 17 * S + 700000), 0, "1.7 s: neither 1 nor 2 seconds");
}

ZTEST(pps, test_holdover_and_reacquire)
{
	struct pps_disc d;
	uint64_t utc;

	pps_init(&d, 2000, 1, 5 * S);
	pps_edge(&d, 0);
	pps_set_utc(&d, 50);
	pps_edge(&d, S);
	zassert_equal(d.state, PPS_LOCKED);
	zassert_equal(pps_tick(&d, S + 4 * S), PPS_LOCKED);
	zassert_equal(pps_tick(&d, S + 5 * S + 1), PPS_HOLDOVER, "no pulse for > 5 s");
	zassert_ok(pps_local_to_utc_ms(&d, 8 * S, &utc), "still converts by extrapolation");
	zassert_equal(utc, 58 * 1000);

	/* pulses resume: the first edge after the gap re-aligns UTC (8 whole
	 * seconds elapsed) but lock needs a clean 1 s interval again. */
	zassert_equal(pps_edge(&d, 9 * S), 1);
	zassert_equal(d.utc_s_at_edge, 59);
	zassert_equal(d.state, PPS_HOLDOVER);
	zassert_equal(pps_edge(&d, 10 * S), 1);
	zassert_equal(d.state, PPS_LOCKED);
	zassert_equal(d.utc_s_at_edge, 60);
}

ZTEST(pps, test_local_clock_wraparound)
{
	struct pps_disc d;
	uint64_t utc;
	uint32_t t = UINT32_MAX - 1500000u;

	pps_init(&d, 2000, 1, 0);
	pps_edge(&d, t);
	pps_set_utc(&d, 7);
	zassert_equal(pps_edge(&d, t + S), 1);
	zassert_equal(pps_edge(&d, t + 2 * S), 1, "edge past the 32-bit wrap");
	zassert_equal(d.utc_s_at_edge, 9);
	zassert_ok(pps_local_to_utc_ms(&d, t + 2 * S + 123000, &utc));
	zassert_equal(utc, 9123);
}
