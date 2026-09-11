/* RS-485 bus master sweep (blueprint §5.2): polled by address in order,
 * silent pods marked missing without stalling, replies from the wrong
 * pod ignored.  Frames come from Code A's protocol definition. */
#include <string.h>
#include <zephyr/ztest.h>
#include <hn/bus_master.h>

/* fake bus: records every transmitted frame, parsed */
static struct {
	struct rs485_frame tx[64];
	int n;
} bus_log;

static int fake_send(void *ctx, const uint8_t *buf, size_t len)
{
	struct rs485_parser p;

	ARG_UNUSED(ctx);
	rs485_parser_init(&p);
	for (size_t i = 0; i < len; i++) {
		if (rs485_parser_byte(&p, buf[i], 0) && bus_log.n < 64) {
			bus_log.tx[bus_log.n++] = p.frame;
		}
	}
	return 0;
}

static struct bus_master b;
static const uint8_t addrs[] = {3, 7, 12};

static void reply(uint8_t addr, uint32_t now, uint8_t *idx, enum bus_event *ev)
{
	struct rs485_frame f = {.addr = addr, .cmd = RS485_CMD_REPORT, .len = 3,
				.payload = {addr, 0xAB, 0xCD}};

	*ev = bus_rx_frame(&b, &f, now, idx);
}

static void before(void *f)
{
	ARG_UNUSED(f);
	memset(&bus_log, 0, sizeof(bus_log));
	zassert_ok(bus_master_init(&b, addrs, ARRAY_SIZE(addrs), 100, 5, fake_send, NULL));
}

ZTEST_SUITE(bus_master, NULL, NULL, before, NULL, NULL);

ZTEST(bus_master, test_sweep_polls_each_address_in_order)
{
	uint8_t idx;
	enum bus_event ev;
	uint32_t t = 1000;

	zassert_ok(bus_sweep_start(&b, t));
	zassert_equal(bus_log.n, 1);
	zassert_equal(bus_log.tx[0].addr, 3);
	zassert_equal(bus_log.tx[0].cmd, RS485_CMD_POLL);

	reply(3, t + 20, &idx, &ev);
	zassert_equal(ev, BUS_EVT_REPORT);
	zassert_equal(idx, 0);
	zassert_equal(b.pods[0].report_len, 3);
	zassert_equal(b.pods[0].report[1], 0xAB);
	zassert_true(b.pods[0].present);

	zassert_equal(bus_tick(&b, t + 22, &idx), BUS_EVT_NONE, "turnaround gap");
	zassert_equal(bus_log.n, 1);
	zassert_equal(bus_tick(&b, t + 25, &idx), BUS_EVT_NONE);
	zassert_equal(bus_log.n, 2, "next poll after the 5 ms gap");
	zassert_equal(bus_log.tx[1].addr, 7);

	reply(7, t + 40, &idx, &ev);
	zassert_equal(ev, BUS_EVT_REPORT);
	(void)bus_tick(&b, t + 45, &idx);
	zassert_equal(bus_log.tx[2].addr, 12);
	reply(12, t + 60, &idx, &ev);
	zassert_equal(ev, BUS_EVT_REPORT);
	zassert_equal(bus_tick(&b, t + 65, &idx), BUS_EVT_SWEEP_DONE);
	zassert_false(bus_sweep_active(&b));
	zassert_equal(b.sweeps, 1);
	zassert_equal(bus_log.n, 3, "exactly one poll per pod per sweep");
}

ZTEST(bus_master, test_silent_pod_marked_missing_without_stalling)
{
	uint8_t idx;
	enum bus_event ev;
	uint32_t t = 0;

	zassert_ok(bus_sweep_start(&b, t));
	reply(3, t + 10, &idx, &ev);
	(void)bus_tick(&b, t + 15, &idx); /* polls 7 */
	zassert_equal(bus_log.tx[1].addr, 7);

	/* pod 7 never answers */
	zassert_equal(bus_tick(&b, t + 15 + 99, &idx), BUS_EVT_NONE);
	zassert_equal(bus_tick(&b, t + 15 + 100, &idx), BUS_EVT_MISSING);
	zassert_equal(idx, 1);
	zassert_false(b.pods[1].present);
	zassert_equal(b.pods[1].misses, 1);
	zassert_equal(b.pods[1].timeouts, 1);

	/* sweep moves straight on to pod 12 */
	zassert_equal(bus_tick(&b, t + 15 + 100 + 5, &idx), BUS_EVT_NONE);
	zassert_equal(bus_log.n, 3);
	zassert_equal(bus_log.tx[2].addr, 12);
	reply(12, t + 130, &idx, &ev);
	zassert_equal(ev, BUS_EVT_REPORT);
	zassert_equal(bus_tick(&b, t + 136, &idx), BUS_EVT_SWEEP_DONE);
	zassert_true(b.pods[0].present);
	zassert_true(b.pods[2].present);
}

ZTEST(bus_master, test_all_silent_bounded_duration)
{
	uint8_t idx;
	uint32_t t = 0, worst = bus_sweep_worst_case_ms(&b);
	int done = 0, missing = 0;

	zassert_equal(worst, 3 * (100 + 5));
	zassert_ok(bus_sweep_start(&b, t));
	for (t = 1; t <= worst + 1; t++) {
		enum bus_event ev = bus_tick(&b, t, &idx);

		if (ev == BUS_EVT_MISSING) {
			missing++;
		} else if (ev == BUS_EVT_SWEEP_DONE) {
			done++;
			break;
		}
	}
	zassert_equal(missing, 3, "every pod marked missing");
	zassert_equal(done, 1, "sweep finished inside its worst-case bound");
	zassert_true(t <= worst + 1, "took %u ms, bound %u", t, worst);
	zassert_equal(b.sweeps, 1);
	for (int i = 0; i < 3; i++) {
		zassert_false(b.pods[i].present);
		zassert_equal(b.pods[i].misses, 1);
	}
}

ZTEST(bus_master, test_wrong_address_and_late_replies_ignored)
{
	uint8_t idx = 99;
	enum bus_event ev;
	struct rs485_frame f = {.addr = 3, .cmd = RS485_CMD_REPORT, .len = 0};

	zassert_ok(bus_sweep_start(&b, 0));
	reply(7, 5, &idx, &ev); /* pod 7 answers while 3 is being polled */
	zassert_equal(ev, BUS_EVT_NONE);
	zassert_false(b.pods[1].present, "a reply for another address is not credited");
	zassert_equal(idx, 99);

	f.cmd = RS485_CMD_POLL; /* someone echoing a poll: not a report */
	zassert_equal(bus_rx_frame(&b, &f, 6, &idx), BUS_EVT_NONE);
	zassert_equal(b.pods[0].bad_replies, 1);

	reply(3, 10, &idx, &ev);
	zassert_equal(ev, BUS_EVT_REPORT);
	reply(3, 12, &idx, &ev);
	zassert_equal(ev, BUS_EVT_NONE, "duplicate/late reply after the slot moved on");
	zassert_equal(b.pods[0].replies, 1);
}

ZTEST(bus_master, test_missing_pod_recovers_next_sweep)
{
	uint8_t idx;
	enum bus_event ev;
	uint32_t t = 0;

	zassert_ok(bus_sweep_start(&b, t));
	for (t = 1; !(bus_tick(&b, t, &idx) == BUS_EVT_SWEEP_DONE); t++) {
	}
	zassert_equal(b.pods[1].misses, 1);

	zassert_ok(bus_sweep_start(&b, t + 1000));
	reply(3, t + 1010, &idx, &ev);
	(void)bus_tick(&b, t + 1015, &idx);
	reply(7, t + 1020, &idx, &ev);
	zassert_equal(ev, BUS_EVT_REPORT);
	zassert_true(b.pods[1].present);
	zassert_equal(b.pods[1].misses, 0, "misses reset on a reply");
}

ZTEST(bus_master, test_start_guards_and_arm_broadcast)
{
	struct bus_master empty;
	uint8_t none[1] = {0};

	zassert_ok(bus_master_init(&empty, none, 0, 100, 5, fake_send, NULL));
	zassert_equal(bus_sweep_start(&empty, 0), -ENODEV, "no pods configured");

	zassert_ok(bus_sweep_start(&b, 0));
	zassert_equal(bus_sweep_start(&b, 1), -EBUSY, "one sweep at a time");
	zassert_equal(bus_send_arm(&b, RS485_ADDR_BROADCAST, true), -EBUSY, "not mid-sweep");

	uint8_t bad[] = {0};

	zassert_equal(bus_master_init(&empty, bad, 1, 100, 5, fake_send, NULL), -EINVAL);

	before(NULL);
	zassert_ok(bus_send_arm(&b, RS485_ADDR_BROADCAST, true));
	zassert_equal(bus_log.tx[0].addr, RS485_ADDR_BROADCAST);
	zassert_equal(bus_log.tx[0].cmd, RS485_CMD_ARM);
	zassert_equal(bus_log.tx[0].payload[0], 1);
}
