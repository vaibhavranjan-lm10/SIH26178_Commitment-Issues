/* Power sequencing (§6.4), siren actuator (§6.8), MsgPack-RPC bridge contract. */
#include <string.h>
#include <zephyr/ztest.h>
#include <hn/alert.h>
#include <hn/bridge_contract.h>
#include <hn/mprpc.h>
#include <hn/rails.h>

/* ---------------- rails ---------------- */

static struct {
	bool on[RAILS_MAX];
	int log_rail[32];
	bool log_on[32];
	int n;
} rl;

static int fake_set(void *ctx, uint8_t rail, bool on)
{
	ARG_UNUSED(ctx);
	rl.on[rail] = on;
	if (rl.n < 32) {
		rl.log_rail[rl.n] = rail;
		rl.log_on[rl.n] = on;
		rl.n++;
	}
	return 0;
}

enum { R_MAST, R_RADIO, R_MODEM };
static const struct rail_cfg cfg[] = {
	[R_MAST] = {"12V mast", 200, 2, 11800, 12400},
	[R_RADIO] = {"3V3 radio", 50, 1, 11400, 12000},
	[R_MODEM] = {"modem", 100, 3, 12000, 12600},
};
static struct rails r;

static void rails_before(void)
{
	memset(&rl, 0, sizeof(rl));
	zassert_ok(rails_init(&r, cfg, ARRAY_SIZE(cfg), fake_set, NULL, 0));
}

ZTEST_SUITE(rails_alert_rpc, NULL, NULL, NULL, NULL, NULL);

ZTEST(rails_alert_rpc, test_rails_bring_up_in_order_with_settle)
{
	rails_before();
	zassert_equal(rl.n, 3, "init drives every rail off");
	for (int i = 0; i < 3; i++) {
		rails_request(&r, i, true);
	}
	rails_tick(&r, 0, 12800);
	zassert_true(rl.on[R_MAST]);
	zassert_false(rl.on[R_RADIO], "waits for the mast bus to settle");
	rails_tick(&r, 199, 12800);
	zassert_false(rl.on[R_RADIO]);
	rails_tick(&r, 200, 12800);
	zassert_true(rl.on[R_RADIO]);
	zassert_false(rl.on[R_MODEM]);
	rails_tick(&r, 250, 12800);
	zassert_true(rl.on[R_MODEM]);
	zassert_false(rails_all_settled(&r));
	rails_tick(&r, 350, 12800);
	zassert_true(rails_all_settled(&r));
	zassert_equal(rl.log_rail[3], R_MAST);
	zassert_equal(rl.log_rail[4], R_RADIO);
	zassert_equal(rl.log_rail[5], R_MODEM);
}

ZTEST(rails_alert_rpc, test_rails_shed_by_priority_and_restore_with_hysteresis)
{
	rails_before();
	for (int i = 0; i < 3; i++) {
		rails_request(&r, i, true);
	}
	for (uint32_t t = 0; t <= 400; t += 10) {
		rails_tick(&r, t, 12800);
	}
	zassert_true(rails_all_settled(&r));

	rails_tick(&r, 500, 11900); /* below modem's 12.0 V only */
	zassert_false(rl.on[R_MODEM], "highest priority sheds first");
	zassert_true(rl.on[R_MAST]);
	zassert_true(rl.on[R_RADIO]);
	rails_tick(&r, 600, 11700);
	zassert_false(rl.on[R_MAST], "then the mast bus");
	zassert_true(rl.on[R_RADIO], "radios hold on longest");
	rails_tick(&r, 700, 11300);
	zassert_false(rl.on[R_RADIO]);
	zassert_equal(r.sheds, 3);

	/* Recovering: nothing comes back until its restore threshold. */
	rails_tick(&r, 800, 11900);
	zassert_false(rl.on[R_MAST]);
	zassert_false(rl.on[R_RADIO]);
	rails_tick(&r, 900, 12050);
	zassert_true(rl.on[R_RADIO], "radio restores above 12.0 V");
	zassert_false(rl.on[R_MAST], "mast needs 12.4 V");
	rails_tick(&r, 1000, 12450);
	zassert_true(rl.on[R_MAST]);
	zassert_false(rl.on[R_MODEM], "modem needs 12.6 V");
	rails_tick(&r, 1300, 12700);
	zassert_true(rl.on[R_MODEM]);
	zassert_equal(r.restores, 3);
}

ZTEST(rails_alert_rpc, test_rails_validation_and_request_off)
{
	static const struct rail_cfg bad[] = {{"x", 0, 1, 12000, 11000}};
	struct rails rr;

	zassert_equal(rails_init(&rr, bad, 1, fake_set, NULL, 0), -EINVAL, "restore <= shed");
	rails_before();
	rails_request(&r, R_RADIO, true);
	rails_tick(&r, 0, 12800);
	rails_tick(&r, 60, 12800);
	zassert_true(rails_is_on(&r, R_RADIO));
	rails_request(&r, R_RADIO, false);
	rails_tick(&r, 70, 12800);
	zassert_false(rails_is_on(&r, R_RADIO));
	zassert_true(rails_all_settled(&r));
}

/* ---------------- alert ---------------- */

ZTEST(rails_alert_rpc, test_alert_warning_continuous_then_expires)
{
	struct alert_out a;

	alert_init(&a, 1000, 2000, 600000);
	zassert_false(alert_tick(&a, 0));
	zassert_ok(alert_command(&a, ALERT_WARNING, 30000, 1000));
	zassert_true(alert_tick(&a, 1000));
	zassert_true(alert_tick(&a, 15000));
	zassert_equal(alert_remaining_ms(&a, 15000), 16000);
	zassert_true(alert_tick(&a, 30999));
	zassert_false(alert_tick(&a, 31000), "duration elapsed");
	zassert_equal(a.expiries, 1);
	zassert_equal(a.level, ALERT_OFF);
}

ZTEST(rails_alert_rpc, test_alert_advisory_pattern_silence_and_cap)
{
	struct alert_out a;

	alert_init(&a, 1000, 2000, 600000);
	zassert_ok(alert_command(&a, ALERT_ADVISORY, 60000, 0));
	zassert_true(alert_tick(&a, 0));
	zassert_true(alert_tick(&a, 999));
	zassert_false(alert_tick(&a, 1000), "1 s on, 2 s off");
	zassert_false(alert_tick(&a, 2999));
	zassert_true(alert_tick(&a, 3000));

	zassert_ok(alert_command(&a, ALERT_OFF, 0, 4000), "Linux side silences");
	zassert_false(alert_tick(&a, 4001));
	zassert_equal(alert_remaining_ms(&a, 4001), 0);

	zassert_ok(alert_command(&a, ALERT_WARNING, 3600000, 5000), "1 h request");
	zassert_equal(alert_remaining_ms(&a, 5000), 600000, "capped at 10 min");
	zassert_equal(alert_command(&a, 7, 1000, 0), -EINVAL);
}

/* ---------------- bridge contract (MsgPack-RPC) ---------------- */

ZTEST(rails_alert_rpc, test_bridge_decode_linux_to_mcu_commands)
{
	uint8_t buf[64];
	struct bc_msg m;
	int n;

	n = bc_encode_set_alert(buf, sizeof(buf), 2, 120);
	zassert_ok(bc_decode(buf, (size_t)n, &m));
	zassert_equal(m.type, BC_MSG_SET_ALERT);
	zassert_equal(m.u.set_alert.level, 2);
	zassert_equal(m.u.set_alert.seconds, 120);

	uint8_t addrs[] = {3, 7, 12};

	n = bc_encode_set_pods(buf, sizeof(buf), addrs, 3);
	zassert_ok(bc_decode(buf, (size_t)n, &m));
	zassert_equal(m.type, BC_MSG_SET_PODS);
	zassert_equal(m.u.set_pods.n_addrs, 3);
	zassert_equal(m.u.set_pods.addrs[2], 12);

	n = bc_encode_set_cadence(buf, sizeof(buf), 300);
	zassert_ok(bc_decode(buf, (size_t)n, &m));
	zassert_equal(m.type, BC_MSG_SET_CADENCE);
	zassert_equal(m.u.set_cadence.seconds, 300);

	n = bc_encode_set_armed(buf, sizeof(buf), true);
	zassert_ok(bc_decode(buf, (size_t)n, &m));
	zassert_equal(m.type, BC_MSG_SET_ARMED);
	zassert_true(m.u.set_armed.armed);

	uint8_t emb[16];

	for (int i = 0; i < 16; i++) {
		emb[i] = (uint8_t)(0x10 + i);
	}
	n = bc_encode_set_embedding(buf, sizeof(buf), emb, sizeof(emb));
	zassert_ok(bc_decode(buf, (size_t)n, &m));
	zassert_equal(m.type, BC_MSG_SET_EMBEDDING);
	zassert_equal(m.u.set_embedding.emb_len, 16);
	zassert_equal(m.u.set_embedding.emb[15], 0x10 + 15);

	n = bc_encode_get_status_call(buf, sizeof(buf), 7);
	zassert_ok(bc_decode(buf, (size_t)n, &m));
	zassert_equal(m.type, BC_MSG_GET_STATUS_CALL);
	zassert_equal(m.msg_id, 7);
}

ZTEST(rails_alert_rpc, test_bridge_encode_mcu_to_linux_notifications)
{
	uint8_t buf[64];
	const uint8_t rep[] = {0x01, 0xAB};
	int n = bc_encode_pod_report(buf, sizeof(buf), 7, rep, 2);
	struct bc_msg m;

	zassert_true(n > 0);
	zassert_ok(bc_decode(buf, (size_t)n, &m));
	zassert_equal(m.type, BC_MSG_POD_REPORT);
	zassert_equal(m.u.pod_report.addr, 7);
	zassert_equal(m.u.pod_report.report_len, 2);
	zassert_mem_equal(m.u.pod_report.report, rep, 2);

	n = bc_encode_pod_missing(buf, sizeof(buf), 12);
	zassert_ok(bc_decode(buf, (size_t)n, &m));
	zassert_equal(m.type, BC_MSG_POD_MISSING);
	zassert_equal(m.u.pod_missing.addr, 12);

	n = bc_encode_time_sync(buf, sizeof(buf), 1757376003250ull, "locked");
	zassert_ok(bc_decode(buf, (size_t)n, &m));
	zassert_equal(m.type, BC_MSG_TIME_SYNC);
	zassert_equal(m.u.time_sync.utc_ms, 1757376003250ull);
	zassert_str_equal(m.u.time_sync.state, "locked");

	zassert_equal(bc_encode_pod_report(buf, 8, 7, rep, 2), -1, "buffer too small: -ENOSPC-equivalent");
}

ZTEST(rails_alert_rpc, test_mprpc_scanner_finds_message_boundary_incrementally)
{
	uint8_t buf[64];
	int n = bc_encode_set_cadence(buf, sizeof(buf), 300);
	size_t ml;

	for (int partial = 0; partial < n; partial++) {
		zassert_equal(mp_scan_value(buf, (size_t)partial, &ml), MP_SCAN_NEED_MORE);
	}
	zassert_equal(mp_scan_value(buf, (size_t)n, &ml), MP_SCAN_OK);
	zassert_equal(ml, (size_t)n);
}
