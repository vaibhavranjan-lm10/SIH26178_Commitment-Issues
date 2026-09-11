/*
 * Blueprint §4.4 link logic without a bus or radio: RS-485 framing and
 * address matching (Mode W), slot timing (Mode R), the shared wire
 * format, and the EEPROM link block.
 */
#include <string.h>
#include <zephyr/ztest.h>
#include <prahari/link_cfg.h>
#include <prahari/lora_slot.h>
#include <prahari/rs485.h>
#include <prahari/wire.h>

/* ------------------------------------------------------------------ */
/* wire format                                                          */
/* ------------------------------------------------------------------ */

static struct pod_report sample_report(void)
{
	struct pod_report r;

	memset(&r, 0, sizeof(r));
	r.primary.cycle = 0x1234;
	r.primary.value[PRAHARI_P13_AIR_TEMP_UNDERSTORY] = 23456;
	r.primary.flags[PRAHARI_P13_AIR_TEMP_UNDERSTORY] = XDCR_F_VALID;
	r.primary.value[PRAHARI_P1_SOIL_VWC_10CM] = -100;
	r.primary.flags[PRAHARI_P1_SOIL_VWC_10CM] = XDCR_F_VALID | XDCR_F_RANGE;
	r.primary.flags[PRAHARI_P22_PM2_5] = XDCR_F_NOPOWER; /* masked, reason kept */
	r.derived[DER_S15_WIND_U] = -1234;
	r.derived_flags[DER_S15_WIND_U] = XDCR_F_VALID;
	r.wake = true;
	r.self.rail_mv = 3287;
	r.self.rail_valid = true;
	r.self.xdcr_fault = 0x0004;
	r.self.xdcr_range = 0x0003;
	return r;
}

ZTEST_SUITE(link, NULL, NULL, NULL, NULL, NULL);

ZTEST(link, test_crc16_ccitt_false_check_value)
{
	zassert_equal(wire_crc16((const uint8_t *)"123456789", 9), 0x29B1);
}

ZTEST(link, test_report_roundtrip)
{
	struct pod_report r = sample_report();
	struct wire_report d;
	uint8_t buf[WIRE_REPORT_MAX];
	int n = wire_encode_report(&r, POD_POS_U, buf, sizeof(buf));

	zassert_true(n > 0);
	zassert_equal(n, 15 + 3 * 6 + 1 * 6, "13 B header + 2 counts + 3 primaries (incl. masked) + 1 derived");
	zassert_ok(wire_decode_report(buf, n, &d));
	zassert_equal(d.position, POD_POS_U);
	zassert_equal(d.cycle, 0x1234);
	zassert_true(d.flags & WIRE_RF_WAKE);
	zassert_true(d.flags & WIRE_RF_RAIL_VALID);
	zassert_equal(d.rail_mv, 3287);
	zassert_equal(d.xdcr_fault, 4);
	zassert_equal(d.xdcr_range, 3);
	zassert_equal(d.n_primary, 3);
	zassert_equal(d.primary[0].id, PRAHARI_P1_SOIL_VWC_10CM);
	zassert_equal(d.primary[0].value, -100);
	zassert_equal(d.primary[0].flags, XDCR_F_VALID | XDCR_F_RANGE);
	zassert_equal(d.primary[1].id, PRAHARI_P13_AIR_TEMP_UNDERSTORY);
	zassert_equal(d.primary[1].value, 23456);
	zassert_equal(d.primary[2].id, PRAHARI_P22_PM2_5);
	zassert_equal(d.primary[2].flags, XDCR_F_NOPOWER, "mask + reason survive the wire");
	zassert_equal(d.n_derived, 1);
	zassert_equal(d.derived[0].id, WIRE_S15);
	zassert_equal(d.derived[0].value, -1234);

	zassert_equal(wire_decode_report(buf, n - 1, &d), -EBADMSG, "truncated");
	zassert_equal(wire_decode_report(buf, n + 1, &d), -EBADMSG, "trailing byte");
	buf[0] = 9;
	zassert_equal(wire_decode_report(buf, n, &d), -EINVAL, "version");
}

ZTEST(link, test_full_pod_fits_one_lora_packet)
{
	struct pod_report r;
	uint8_t buf[WIRE_REPORT_MAX], pkt[256];
	int n, m;

	memset(&r, 0, sizeof(r));
	/* worst realistic pod: 12 primaries + 5 derived */
	for (uint8_t p = 1; p <= 12; p++) {
		r.primary.flags[p] = XDCR_F_VALID;
	}
	for (int d = 0; d < DER_COUNT; d++) {
		r.derived_flags[d] = XDCR_F_VALID;
	}
	n = wire_encode_report(&r, POD_POS_G, buf, sizeof(buf));
	zassert_equal(n, 15 + 17 * 6);
	m = wire_encode_lora_uplink(7, buf, n, pkt, sizeof(pkt));
	zassert_equal(m, n + 5);
	zassert_true(m <= 255, "one LoRa packet");
}

ZTEST(link, test_beacon_and_uplink_framing)
{
	struct wire_beacon b = {.superframe_idx = 42, .slot_ms = 2000, .n_slots = 8,
				.beacon_ms = 500},
			   d;
	uint8_t buf[WIRE_BEACON_LEN], pkt[64];
	const uint8_t *rep;
	size_t rlen;
	uint8_t addr;

	zassert_equal(wire_encode_beacon(&b, buf, sizeof(buf)), WIRE_BEACON_LEN);
	zassert_ok(wire_decode_beacon(buf, sizeof(buf), &d));
	zassert_equal(d.superframe_idx, 42);
	zassert_equal(d.slot_ms, 2000);
	zassert_equal(d.n_slots, 8);
	zassert_equal(d.beacon_ms, 500);
	buf[3] ^= 0x80;
	zassert_equal(wire_decode_beacon(buf, sizeof(buf), &d), -EBADMSG);

	zassert_equal(wire_encode_lora_uplink(5, (const uint8_t *)"abc", 3, pkt, sizeof(pkt)), 8);
	zassert_ok(wire_decode_lora_uplink(pkt, 8, &addr, &rep, &rlen));
	zassert_equal(addr, 5);
	zassert_equal(rlen, 3);
	zassert_mem_equal(rep, "abc", 3);
	pkt[4] = 'X';
	zassert_equal(wire_decode_lora_uplink(pkt, 8, &addr, &rep, &rlen), -EBADMSG);
}

/* ------------------------------------------------------------------ */
/* Mode W: RS-485 framing + address matching                            */
/* ------------------------------------------------------------------ */

static bool feed(struct rs485_parser *p, const uint8_t *buf, size_t n, uint32_t t)
{
	bool done = false;

	for (size_t i = 0; i < n; i++) {
		done = rs485_parser_byte(p, buf[i], t);
	}
	return done;
}

ZTEST(link, test_rs485_frame_encode_parse_roundtrip)
{
	struct rs485_frame f = {.addr = 3, .cmd = RS485_CMD_POLL, .len = 2, .payload = {0xAA, 0x7E}};
	struct rs485_parser p;
	uint8_t buf[RS485_FRAME_MAX];
	int n = rs485_frame_encode(&f, buf, sizeof(buf));

	zassert_equal(n, 8);
	zassert_equal(buf[0], RS485_SOF);
	rs485_parser_init(&p);
	zassert_true(feed(&p, buf, n, 0), "SOF byte inside payload must not confuse the parser");
	zassert_equal(p.frame.addr, 3);
	zassert_equal(p.frame.cmd, RS485_CMD_POLL);
	zassert_equal(p.frame.len, 2);
	zassert_equal(p.frame.payload[1], 0x7E);
	zassert_equal(p.frames, 1);
	zassert_equal(p.crc_errors, 0);
}

ZTEST(link, test_rs485_parser_resyncs_after_garbage_and_crc_error)
{
	struct rs485_frame f = {.addr = 9, .cmd = RS485_CMD_PING, .len = 0};
	struct rs485_parser p;
	uint8_t good[RS485_FRAME_MAX], bad[RS485_FRAME_MAX];
	int n = rs485_frame_encode(&f, good, sizeof(good));
	const uint8_t noise[] = {0x00, 0x12, 0x7E, 0x09, 0x03}; /* a truncated frame start */

	rs485_parser_init(&p);
	memcpy(bad, good, n);
	bad[n - 1] ^= 0xFF; /* corrupt CRC */
	zassert_false(feed(&p, bad, n, 0));
	zassert_equal(p.crc_errors, 1);
	zassert_true(feed(&p, good, n, 1), "recovers immediately after a bad frame");

	/* Partial frame then an idle gap: parser must drop it and take the next frame. */
	zassert_false(feed(&p, noise, sizeof(noise), 10));
	rs485_parser_idle(&p, 10 + 4, 3);
	zassert_equal(p.state, RS485_RX_SOF, "idle gap resets");
	zassert_true(feed(&p, good, n, 20));
	zassert_equal(p.frames, 2);

	/* Oversized length byte is rejected up front. */
	const uint8_t huge[] = {RS485_SOF, 9, RS485_CMD_POLL, RS485_PAYLOAD_MAX + 1};

	zassert_false(feed(&p, huge, sizeof(huge), 30));
	zassert_equal(p.state, RS485_RX_SOF);
}

static struct {
	int calls;
	bool armed;
} arm_log;

static void arm_cb(void *ctx, bool armed)
{
	ARG_UNUSED(ctx);
	arm_log.calls++;
	arm_log.armed = armed;
}

static const uint8_t my_report[] = {1, 2, 3, 4, 5};
static const struct rs485_slave me = {.addr = 7, .position = 4, .fw_major = 0, .fw_minor = 1,
				      .report = my_report, .report_len = sizeof(my_report),
				      .arm_cb = arm_cb};

ZTEST(link, test_rs485_polled_pod_replies_with_report)
{
	struct rs485_frame in = {.addr = 7, .cmd = RS485_CMD_POLL, .len = 0}, out;

	zassert_equal(rs485_slave_handle(&me, &in, &out), 1);
	zassert_equal(out.addr, 7, "reply carries the pod's own address");
	zassert_equal(out.cmd, RS485_CMD_REPORT);
	zassert_equal(out.len, sizeof(my_report));
	zassert_mem_equal(out.payload, my_report, sizeof(my_report));
}

ZTEST(link, test_rs485_unpolled_pod_stays_silent)
{
	struct rs485_frame in = {.addr = 8, .cmd = RS485_CMD_POLL, .len = 0}, out;

	/* §5.2: the master sweeps addresses; a pod that is not addressed
	 * says nothing and needs no logic to notice it was skipped. */
	zassert_equal(rs485_slave_handle(&me, &in, &out), 0);
	in.addr = RS485_ADDR_MASTER;
	zassert_equal(rs485_slave_handle(&me, &in, &out), 0);

	/* Another pod's REPORT going past on the shared bus is never answered. */
	in.addr = 7;
	in.cmd = RS485_CMD_REPORT;
	zassert_equal(rs485_slave_handle(&me, &in, &out), 0);

	/* A broadcast POLL would make every pod talk at once: ignored. */
	in.addr = RS485_ADDR_BROADCAST;
	in.cmd = RS485_CMD_POLL;
	zassert_equal(rs485_slave_handle(&me, &in, &out), 0);
}

ZTEST(link, test_rs485_no_report_yet_replies_empty)
{
	struct rs485_slave fresh = me;
	struct rs485_frame in = {.addr = 7, .cmd = RS485_CMD_POLL, .len = 0}, out;

	fresh.report = NULL;
	fresh.report_len = 0;
	zassert_equal(rs485_slave_handle(&fresh, &in, &out), 1);
	zassert_equal(out.cmd, RS485_CMD_REPORT);
	zassert_equal(out.len, 0, "answers the poll, with nothing to say");
}

ZTEST(link, test_rs485_arm_unicast_acks_broadcast_silent)
{
	struct rs485_frame in = {.addr = 7, .cmd = RS485_CMD_ARM, .len = 1, .payload = {1}}, out;

	memset(&arm_log, 0, sizeof(arm_log));
	zassert_equal(rs485_slave_handle(&me, &in, &out), 1);
	zassert_equal(out.cmd, RS485_CMD_ACK);
	zassert_equal(arm_log.calls, 1);
	zassert_true(arm_log.armed);

	in.addr = RS485_ADDR_BROADCAST;
	in.payload[0] = 0;
	zassert_equal(rs485_slave_handle(&me, &in, &out), 0, "broadcast: act, do not reply");
	zassert_equal(arm_log.calls, 2);
	zassert_false(arm_log.armed);

	in.addr = 9; /* someone else's arm command */
	zassert_equal(rs485_slave_handle(&me, &in, &out), 0);
	zassert_equal(arm_log.calls, 2);
}

ZTEST(link, test_rs485_ping_and_unknown_command)
{
	struct rs485_frame in = {.addr = 7, .cmd = RS485_CMD_PING, .len = 0}, out;

	zassert_equal(rs485_slave_handle(&me, &in, &out), 1);
	zassert_equal(out.cmd, RS485_CMD_PONG);
	zassert_equal(out.payload[0], 4, "position");
	zassert_equal(out.payload[2], 1, "fw minor");

	in.cmd = 0x55;
	zassert_equal(rs485_slave_handle(&me, &in, &out), 1);
	zassert_equal(out.cmd, RS485_CMD_NAK);
	zassert_equal(out.payload[0], 0x55);

	in.addr = RS485_ADDR_BROADCAST;
	zassert_equal(rs485_slave_handle(&me, &in, &out), 0, "no NAK storm on broadcast");
}

ZTEST(link, test_rs485_address_validity)
{
	struct rs485_slave bad = me;
	struct rs485_frame in = {.addr = 0, .cmd = RS485_CMD_POLL, .len = 0}, out;

	zassert_true(rs485_addr_valid(1));
	zassert_true(rs485_addr_valid(247));
	zassert_false(rs485_addr_valid(0), "0 is the master");
	zassert_false(rs485_addr_valid(255), "255 is broadcast");
	bad.addr = 0;
	zassert_equal(rs485_slave_handle(&bad, &in, &out), -EINVAL);
}

/* ------------------------------------------------------------------ */
/* Mode R: slot timing                                                  */
/* ------------------------------------------------------------------ */

ZTEST(link, test_slot_geometry_validation)
{
	struct slot_sched s;

	zassert_ok(slot_sched_init(&s, 60000, 2000, 500, 8, 3, 50, 3600000));
	zassert_equal(slot_sched_init(&s, 60000, 2000, 500, 8, 8, 50, 0), -EINVAL, "slot >= n");
	zassert_equal(slot_sched_init(&s, 10000, 2000, 500, 8, 0, 50, 0), -EINVAL,
		      "8 x 2 s + beacon do not fit in 10 s");
	zassert_equal(slot_sched_init(&s, 60000, 0, 500, 8, 0, 50, 0), -EINVAL);
}

ZTEST(link, test_slot_next_tx_from_beacon)
{
	struct slot_sched s;
	uint32_t tx, rx;

	zassert_ok(slot_sched_init(&s, 60000, 2000, 500, 8, 3, 50, 3600000));
	zassert_equal(slot_sched_next_tx(&s, 1000, &tx), -EAGAIN, "no epoch before a beacon");

	/* Beacon heard at local t = 10 000, 120 ms after it started. */
	slot_sched_sync(&s, 10000, 120);
	zassert_equal(s.epoch_ms, 9880);
	zassert_true(s.synced);

	/* Slot 3 = epoch + 500 + 3*2000 = 16 380 */
	zassert_ok(slot_sched_next_tx(&s, 10000, &tx));
	zassert_equal(tx, 16380);
	/* Just before the slot: still this superframe. */
	zassert_ok(slot_sched_next_tx(&s, 16330, &tx));
	zassert_equal(tx, 16380);
	/* Inside the guard: too late, next superframe. */
	zassert_ok(slot_sched_next_tx(&s, 16331, &tx));
	zassert_equal(tx, 16380 + 60000);
	/* Long after: k superframes ahead. */
	zassert_ok(slot_sched_next_tx(&s, 16380 + 60000 * 5 + 1, &tx));
	zassert_equal(tx, 16380 + 60000 * 6);

	/* Next beacon window opens at the next superframe start. */
	zassert_ok(slot_sched_next_beacon(&s, 20000, &rx));
	zassert_equal(rx, 9880 + 60000);
}

ZTEST(link, test_slot_zero_and_last_slot_edges)
{
	struct slot_sched s;
	uint32_t tx;

	zassert_ok(slot_sched_init(&s, 60000, 2000, 500, 8, 0, 0, 0));
	slot_sched_sync(&s, 0, 0);
	zassert_ok(slot_sched_next_tx(&s, 0, &tx));
	zassert_equal(tx, 500, "slot 0 starts right after the beacon airtime");

	zassert_ok(slot_sched_init(&s, 60000, 2000, 500, 8, 7, 0, 0));
	slot_sched_sync(&s, 0, 0);
	zassert_ok(slot_sched_next_tx(&s, 0, &tx));
	zassert_equal(tx, 500 + 7 * 2000);
	zassert_true(tx + 2000 <= 60000, "last slot ends inside the superframe");
}

ZTEST(link, test_slot_uptime_wraparound)
{
	struct slot_sched s;
	uint32_t tx, t0 = UINT32_MAX - 30000;

	zassert_ok(slot_sched_init(&s, 60000, 2000, 500, 8, 5, 50, 3600000));
	slot_sched_sync(&s, t0, 0);
	/* slot 5 = epoch + 10 500 -> wraps past UINT32_MAX */
	zassert_ok(slot_sched_next_tx(&s, t0, &tx));
	zassert_equal(tx, (uint32_t)(t0 + 10500));
	zassert_ok(slot_sched_next_tx(&s, (uint32_t)(t0 + 20000), &tx));
	zassert_equal(tx, (uint32_t)(t0 + 10500 + 60000), "after the wrap, next superframe");
}

ZTEST(link, test_slot_holdover_keeps_reporting)
{
	struct slot_sched s;
	uint32_t tx;

	zassert_ok(slot_sched_init(&s, 60000, 2000, 500, 8, 2, 50, 300000));
	/* Never heard a beacon: free-running schedule from boot. */
	slot_sched_start_free_running(&s, 5000);
	zassert_false(s.synced);
	zassert_ok(slot_sched_next_tx(&s, 5000, &tx), "still transmits without the head node");
	zassert_equal(tx, 5000 + 500 + 2 * 2000);

	/* Sync, then lose the beacon for longer than holdover. */
	slot_sched_sync(&s, 65000, 100);
	zassert_true(slot_sched_update(&s, 65000 + 299999));
	zassert_false(slot_sched_update(&s, 65000 + 300001), "unsynced after holdover");
	zassert_ok(slot_sched_next_tx(&s, 65000 + 300001, &tx), "but keeps its slot cadence");
	zassert_equal((tx - s.epoch_ms - 500 - 2 * 2000) % 60000, 0);

	/* A fresh beacon re-syncs and re-aligns the epoch. */
	slot_sched_sync(&s, 65000 + 360000 + 37, 100);
	zassert_true(s.synced);
	zassert_equal(s.epoch_ms, 65000 + 360000 + 37 - 100);
}

ZTEST(link, test_lora_time_on_air_fits_slot)
{
	/* SX126x datasheet formula, 125 kHz, CR 4/5, 8-symbol preamble,
	 * explicit header, CRC on.  Tsym = 4.096 ms at SF9, 8.192 ms at SF10.
	 *   107 B @ SF9 : 12.25 pre + 8 + 24*5 payload symbols = 574.5 ms
	 *    20 B @ SF9 : 12.25 + 8 + 5*5                        = 185.3 ms
	 *   107 B @ SF10: 12.25 + 8 + 22*5                       = 1067 ms  */
	uint32_t t107 = lora_time_on_air_ms(107, 9, 125000, 5, 8, true, true, false);
	uint32_t t20 = lora_time_on_air_ms(20, 9, 125000, 5, 8, true, true, false);
	uint32_t t107_sf10 = lora_time_on_air_ms(107, 10, 125000, 5, 8, true, true, false);

	zassert_within(t107, 575, 2);
	zassert_within(t20, 186, 2);
	zassert_within(t107_sf10, 1068, 3);
	zassert_true(t107_sf10 + 2 * 50 < 2000, "worst-case pod packet at SF10 fits a 2 s slot");
	zassert_equal(lora_time_on_air_ms(10, 9, 125000, 9, 8, true, true, false), 0, "bad CR");
}

/* ------------------------------------------------------------------ */
/* EEPROM link block                                                    */
/* ------------------------------------------------------------------ */

ZTEST(link, test_link_cfg_eeprom_block)
{
	const struct link_cfg dflt = {.addr = 1, .slot = 0};
	struct link_cfg c = {.addr = 42, .slot = 5}, out;
	enum link_cfg_source src;
	uint8_t blob[LINK_CFG_BLOB_LEN];

	zassert_ok(link_cfg_encode(&c, blob, sizeof(blob)));
	zassert_ok(link_cfg_resolve(blob, sizeof(blob), &dflt, &out, &src));
	zassert_equal(out.addr, 42);
	zassert_equal(out.slot, 5);
	zassert_equal(src, LINK_CFG_SRC_EEPROM);

	blob[3] = 0; /* address 0 is the master: invalid even with a good CRC */
	blob[5] = pod_config_crc8(blob, 5);
	zassert_ok(link_cfg_resolve(blob, sizeof(blob), &dflt, &out, &src));
	zassert_equal(out.addr, 1);
	zassert_equal(src, LINK_CFG_SRC_KCONFIG);

	memset(blob, 0xFF, sizeof(blob));
	zassert_ok(link_cfg_resolve(blob, sizeof(blob), &dflt, &out, &src));
	zassert_equal(src, LINK_CFG_SRC_KCONFIG);
	zassert_ok(link_cfg_resolve(NULL, 0, &dflt, &out, NULL));
	zassert_equal(out.addr, 1);

	c.addr = 255;
	zassert_equal(link_cfg_encode(&c, blob, sizeof(blob)), -EINVAL);
}
