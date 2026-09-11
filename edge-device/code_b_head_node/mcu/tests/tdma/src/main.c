/* LoRa TDMA slot timing (blueprint §7.3/§7.4): hash-assigned mesh slot,
 * UTC-aligned superframes, pod beacon/uplink region, mesh region. */
#include <string.h>
#include <zephyr/ztest.h>
#include <hn/tdma.h>

static const struct tdma_cfg cfg = {
	.superframe_ms = 60000,
	.beacon_ms = 500,
	.pod_slot_ms = 2000,
	.n_pod_slots = 8,
	.mesh_slot_ms = 1000,
	.n_mesh_slots = 16,
	.guard_ms = 50,
};

ZTEST_SUITE(tdma, NULL, NULL, NULL, NULL, NULL);

ZTEST(tdma, test_layout_regions_fit_and_do_not_overlap)
{
	struct tdma_layout l;
	struct tdma_cfg bad = cfg;

	zassert_ok(tdma_layout(&cfg, &l));
	zassert_equal(l.pod_region_ms, 500);
	zassert_equal(l.mesh_region_ms, 500 + 8 * 2000);
	zassert_equal(l.used_ms, 500 + 8 * 2000 + 16 * 1000);
	zassert_true(l.used_ms <= cfg.superframe_ms, "one round per inference cycle");

	bad.n_mesh_slots = 60;
	zassert_equal(tdma_layout(&bad, &l), -EINVAL, "regions must fit the superframe");
	bad = cfg;
	bad.superframe_ms = 0;
	zassert_equal(tdma_layout(&bad, &l), -EINVAL);
}

ZTEST(tdma, test_mesh_slot_from_node_id_hash)
{
	const char *a = "prahari-node-0", *b = "prahari-node-1";
	uint8_t sa = tdma_mesh_slot((const uint8_t *)a, strlen(a), 16);
	uint8_t sb = tdma_mesh_slot((const uint8_t *)b, strlen(b), 16);
	int distinct = 0;

	zassert_true(sa < 16 && sb < 16);
	zassert_equal(sa, tdma_mesh_slot((const uint8_t *)a, strlen(a), 16), "deterministic");
	zassert_equal(tdma_hash((const uint8_t *)"", 0), 2166136261u, "FNV-1a offset basis");
	zassert_equal(tdma_hash((const uint8_t *)"a", 1), 0xe40c292cu, "FNV-1a('a')");

	/* 16 distinct ids into 16 slots: not all collide (spread sanity). */
	for (int i = 0; i < 16; i++) {
		char id[24];

		snprintf(id, sizeof(id), "node-%d", i);
		if (tdma_mesh_slot((const uint8_t *)id, strlen(id), 16) != sa) {
			distinct++;
		}
	}
	zassert_true(distinct >= 8, "hash spreads ids over slots (%d differ)", distinct);
	zassert_equal(tdma_mesh_slot((const uint8_t *)a, strlen(a), 0), 0, "n=0 guarded");
}

ZTEST(tdma, test_utc_aligned_superframes)
{
	/* Two nodes with the same UTC compute the same boundaries: no coordinator. */
	uint64_t t = 1757376000000ull + 12345; /* 2025-09-09T00:00:12.345Z */

	zassert_equal(tdma_superframe_index(&cfg, t), 1757376000000ull / 60000);
	zassert_equal(tdma_offset_in_superframe(&cfg, t), 12345);
	zassert_equal(tdma_next_beacon(&cfg, t), 1757376000000ull + 60000);
	zassert_equal(tdma_next_beacon(&cfg, 1757376000000ull - 50), 1757376000000ull,
		      "50 ms before the boundary: just enough guard, this one");
	zassert_equal(tdma_next_beacon(&cfg, 1757376000000ull - 49), 1757376000000ull + 60000,
		      "49 ms before the boundary is inside the 50 ms guard: next one");
	zassert_equal(tdma_next_beacon(&cfg, 1757376000000ull), 1757376000000ull + 60000,
		      "exactly at the boundary is too late: next one");
}

ZTEST(tdma, test_next_pod_and_mesh_slots)
{
	uint64_t sf0 = 1757376000000ull;

	zassert_equal(tdma_next_pod_slot_start(&cfg, sf0, 0), sf0 + 500);
	zassert_equal(tdma_next_pod_slot_start(&cfg, sf0, 7), sf0 + 500 + 7 * 2000);
	zassert_equal(tdma_next_pod_slot_start(&cfg, sf0 + 500 + 7 * 2000 + 1, 7),
		      sf0 + 60000 + 500 + 7 * 2000, "missed by 1 ms: next superframe");

	zassert_equal(tdma_next_mesh_tx(&cfg, sf0, 0), sf0 + 16500);
	zassert_equal(tdma_next_mesh_tx(&cfg, sf0, 15), sf0 + 16500 + 15000);
	zassert_equal(tdma_next_mesh_tx(&cfg, sf0 + 40000, 3), sf0 + 60000 + 16500 + 3000);
}

ZTEST(tdma, test_slot_lookup_at_instant)
{
	uint64_t sf0 = 1757376000000ull;

	zassert_equal(tdma_pod_slot_at(&cfg, sf0 + 100), -1, "beacon airtime");
	zassert_equal(tdma_pod_slot_at(&cfg, sf0 + 500), 0);
	zassert_equal(tdma_pod_slot_at(&cfg, sf0 + 500 + 2 * 2000 + 1999), 2);
	zassert_equal(tdma_pod_slot_at(&cfg, sf0 + 500 + 8 * 2000), -1, "past the last pod slot");
	zassert_equal(tdma_mesh_slot_at(&cfg, sf0 + 16500), 0);
	zassert_equal(tdma_mesh_slot_at(&cfg, sf0 + 16500 + 15999), 15);
	zassert_equal(tdma_mesh_slot_at(&cfg, sf0 + 16500 + 16000), -1, "idle tail of the superframe");
	zassert_equal(tdma_mesh_slot_at(&cfg, sf0 + 16499), -1);
}

ZTEST(tdma, test_two_nodes_never_share_a_slot_time)
{
	/* Different mesh slots => their TX instants in one superframe differ
	 * by at least one slot length. */
	uint64_t sf0 = 1757376000000ull;
	uint64_t ta = tdma_next_mesh_tx(&cfg, sf0, 4);
	uint64_t tb = tdma_next_mesh_tx(&cfg, sf0, 5);

	zassert_equal(tb - ta, cfg.mesh_slot_ms);
	zassert_equal(tdma_superframe_index(&cfg, ta), tdma_superframe_index(&cfg, tb));
}
