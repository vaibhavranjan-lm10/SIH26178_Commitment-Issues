/* Watchdog supervisor: the hardware watchdog is fed only while every
 * activity checks in inside its deadline. */
#include <zephyr/ztest.h>
#include <hn/supervisor.h>

static struct supervisor s;
static int c_main, c_bus, c_rpc;

static void before(void *f)
{
	ARG_UNUSED(f);
	sup_init(&s, 1000);
	c_main = sup_register(&s, "main", 200, true);
	c_bus = sup_register(&s, "bus", 5000, true);
	c_rpc = sup_register(&s, "rpc", 60000, false);
	zassert_true(c_main >= 0 && c_bus >= 0 && c_rpc >= 0);
}

ZTEST_SUITE(supervisor, NULL, NULL, before, NULL, NULL);

ZTEST(supervisor, test_feed_only_when_all_fresh)
{
	sup_checkin(&s, c_main, 1000);
	sup_checkin(&s, c_bus, 1000);
	zassert_true(sup_should_feed(&s, 1100));
	zassert_equal(sup_stale_mask(&s, 1100), 0);

	/* main loop stalls */
	zassert_true(sup_should_feed(&s, 1200), "exactly at the deadline is still fresh");
	zassert_false(sup_should_feed(&s, 1201), "one ms past: no feed -> reset");
	zassert_equal(sup_stale_mask(&s, 1201), BIT(c_main));
	zassert_equal(s.feeds, 2);
	zassert_equal(s.refusals, 1);

	sup_checkin(&s, c_main, 1201);
	zassert_true(sup_should_feed(&s, 1300), "recovers once it checks in");
}

ZTEST(supervisor, test_required_at_boot_must_arrive)
{
	/* bus never checks in after init: stale after its deadline from init */
	sup_checkin(&s, c_main, 5990);
	zassert_true(sup_should_feed(&s, 5990), "bus deadline (5 s from init) not yet passed");
	sup_checkin(&s, c_main, 6001);
	zassert_false(sup_should_feed(&s, 6001), "bus silent since boot");
	zassert_equal(sup_stale_mask(&s, 6001), BIT(c_bus));
}

ZTEST(supervisor, test_optional_channel_arms_on_first_checkin)
{
	sup_checkin(&s, c_main, 1000);
	sup_checkin(&s, c_bus, 1000);
	zassert_true(sup_should_feed(&s, 1100), "rpc never seen: not required");

	sup_checkin(&s, c_rpc, 1100); /* now it is watched */
	sup_checkin(&s, c_main, 61000);
	sup_checkin(&s, c_bus, 61000);
	zassert_true(sup_should_feed(&s, 61100));
	sup_checkin(&s, c_main, 61200);
	sup_checkin(&s, c_bus, 61200);
	zassert_false(sup_should_feed(&s, 61200), "rpc went quiet for > 60 s");
}

ZTEST(supervisor, test_wraparound_and_limits)
{
	struct supervisor w;
	int c;

	sup_init(&w, UINT32_MAX - 50);
	c = sup_register(&w, "x", 100, true);
	sup_checkin(&w, c, UINT32_MAX - 50);
	zassert_true(sup_should_feed(&w, 49), "99 ms across the wrap");
	zassert_false(sup_should_feed(&w, 51), "101 ms across the wrap");

	sup_init(&w, 0);
	for (int i = 0; i < SUP_MAX_CHANNELS; i++) {
		zassert_true(sup_register(&w, "c", 10, false) >= 0);
	}
	zassert_equal(sup_register(&w, "extra", 10, false), -ENOSPC);
	zassert_equal(sup_register(&w, "zero", 0, false), -ENOSPC, "deadline 0 rejected");
	sup_checkin(&w, 99, 0); /* bad id: ignored */
}
