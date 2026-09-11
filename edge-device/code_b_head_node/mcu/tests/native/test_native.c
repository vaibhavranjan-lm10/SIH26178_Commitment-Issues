/*
 * Host-native (plain gcc, no Zephyr) tests for mprpc.c + bridge_contract.c.
 * Pure C modules under test have zero Zephyr dependency, so this compiles
 * and runs directly on the dev machine — fast feedback before/independent
 * of a full `west build`. The ztest suite under ../mprpc_bridge/ builds
 * the SAME two source files into the Zephyr test image for on-target-class
 * verification; this file exists for speed and for the msgpack interop
 * dump consumed by test_interop.py (real `msgpack` Python package
 * cross-check — the actual point of a "real message contract" claim).
 * SPDX-License-Identifier: Apache-2.0
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <hn/bridge_contract.h>
#include <hn/mprpc.h>

static int failures;
#define CHECK(cond) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		failures++; \
	} \
} while (0)

/* ---------------------------------------------------------------- mprpc */
static void test_int_roundtrip(void)
{
	int64_t vals[] = {0, 1, 127, 128, -1, -32, -33, 255, 256, 65535, 65536,
			  -128, -129, -32768, -32769, 2147483647, -2147483648LL,
			  2147483648LL, -2147483649LL, 1757400000123LL, INT64_MIN, INT64_MAX};

	for (size_t i = 0; i < sizeof(vals) / sizeof(vals[0]); i++) {
		uint8_t buf[16];
		size_t off = 0;

		CHECK(mp_put_int(buf, sizeof(buf), &off, vals[i]) == 0);
		struct mp_reader r;
		int64_t got;

		mp_reader_init(&r, buf, off);
		CHECK(mp_get_int(&r, &got) == 0);
		CHECK(got == vals[i]);
		CHECK(r.pos == off);
	}
}

static void test_str_fixstr_str8_boundary(void)
{
	char s31[31], s32[32];

	memset(s31, 'a', 31);
	memset(s32, 'b', 32);
	uint8_t buf[64];
	size_t off = 0;

	CHECK(mp_put_str(buf, sizeof(buf), &off, s31, 31) == 0);
	CHECK(buf[0] == (0xa0 | 31)); /* fixstr, not str8 */
	CHECK(off == 1 + 31);
	off = 0;
	CHECK(mp_put_str(buf, sizeof(buf), &off, s32, 32) == 0);
	CHECK(buf[0] == 0xd9 && buf[1] == 32); /* str8 */
	CHECK(off == 2 + 32);
	struct mp_reader r;
	const char *ptr;
	size_t len;

	mp_reader_init(&r, buf, off);
	CHECK(mp_get_str(&r, &ptr, &len) == 0 && len == 32 && memcmp(ptr, s32, 32) == 0);
}

static void test_bin_bin8_bin16_boundary(void)
{
	uint8_t data[300];

	for (int i = 0; i < 300; i++) data[i] = (uint8_t)i;
	uint8_t buf[400];
	size_t off = 0;

	CHECK(mp_put_bin(buf, sizeof(buf), &off, data, 255) == 0);
	CHECK(buf[0] == 0xc4 && buf[1] == 255);
	off = 0;
	CHECK(mp_put_bin(buf, sizeof(buf), &off, data, 256) == 0);
	CHECK(buf[0] == 0xc5 && buf[1] == 1 && buf[2] == 0); /* be16(256) */
	struct mp_reader r;
	const uint8_t *ptr;
	size_t len;

	mp_reader_init(&r, buf, off);
	CHECK(mp_get_bin(&r, &ptr, &len) == 0 && len == 256 && memcmp(ptr, data, 256) == 0);
}

static void test_array_fixarray_array16_boundary(void)
{
	uint8_t buf[16];
	size_t off = 0;

	CHECK(mp_put_array_header(buf, sizeof(buf), &off, 15) == 0);
	CHECK(buf[0] == (0x90 | 15));
	off = 0;
	CHECK(mp_put_array_header(buf, sizeof(buf), &off, 16) == 0);
	CHECK(buf[0] == 0xdc && buf[1] == 0 && buf[2] == 16);
	struct mp_reader r;
	size_t n;

	mp_reader_init(&r, buf, off);
	CHECK(mp_get_array_header(&r, &n) == 0 && n == 16);
}

static void test_scan_needs_more_then_ok(void)
{
	uint8_t buf[64];
	size_t off = 0;

	CHECK(bc_encode_set_cadence(buf, sizeof(buf), 300) > 0);
	size_t full = off; /* not set by bc_encode; recompute */
	full = (size_t)bc_encode_set_cadence(buf, sizeof(buf), 300);

	for (size_t partial = 0; partial < full; partial++) {
		size_t ml;
		enum mp_scan_result r = mp_scan_value(buf, partial, &ml);

		CHECK(r == MP_SCAN_NEED_MORE);
	}
	size_t ml;
	enum mp_scan_result r = mp_scan_value(buf, full, &ml);

	CHECK(r == MP_SCAN_OK && ml == full);
	/* trailing garbage after a complete message must not be included */
	uint8_t buf2[80];

	memcpy(buf2, buf, full);
	buf2[full] = 0xff; /* would-be next message start; scanner must stop at `full` */
	buf2[full + 1] = 0x00;
	r = mp_scan_value(buf2, full + 2, &ml);
	CHECK(r == MP_SCAN_OK && ml == full);
}

static void test_scan_rejects_unsupported_type(void)
{
	uint8_t buf[8] = {0xca, 0, 0, 0, 0}; /* float32: never sent, never accepted */
	size_t ml;

	CHECK(mp_scan_value(buf, sizeof(buf), &ml) == MP_SCAN_ERROR);
}

/* ---------------------------------------------------------------- bridge_contract */
static void test_pod_report_roundtrip(void)
{
	uint8_t buf[256];
	uint8_t report[] = {1, 2, 3, 4, 5, 6, 7, 8};
	int n = bc_encode_pod_report(buf, sizeof(buf), 5, report, sizeof(report));

	CHECK(n > 0);
	struct bc_msg m;

	CHECK(bc_decode(buf, (size_t)n, &m) == 0);
	CHECK(m.type == BC_MSG_POD_REPORT);
	CHECK(m.u.pod_report.addr == 5);
	CHECK(m.u.pod_report.report_len == sizeof(report));
	CHECK(memcmp(m.u.pod_report.report, report, sizeof(report)) == 0);
}

static void test_sweep_done_roundtrip(void)
{
	uint8_t buf[64];
	int n = bc_encode_sweep_done(buf, sizeof(buf), 12, 2, 3);
	struct bc_msg m;

	CHECK(n > 0 && bc_decode(buf, (size_t)n, &m) == 0);
	CHECK(m.type == BC_MSG_SWEEP_DONE);
	CHECK(m.u.sweep_done.sweep == 12 && m.u.sweep_done.present == 2 && m.u.sweep_done.total == 3);
}

static void test_time_sync_roundtrip(void)
{
	uint8_t buf[64];
	int n = bc_encode_time_sync(buf, sizeof(buf), 1757400000123ULL, "locked");
	struct bc_msg m;

	CHECK(n > 0 && bc_decode(buf, (size_t)n, &m) == 0);
	CHECK(m.type == BC_MSG_TIME_SYNC);
	CHECK(m.u.time_sync.utc_ms == 1757400000123ULL);
	CHECK(strcmp(m.u.time_sync.state, "locked") == 0);
}

static void test_node_status_and_get_status_roundtrip(void)
{
	struct bc_status st = {.sweeps = 42, .pods = 3, .drift_ppm = -7, .batt_mv = 12810};

	strcpy(st.pps, "holdover");
	uint8_t buf[128];
	int n = bc_encode_node_status(buf, sizeof(buf), &st);
	struct bc_msg m;

	CHECK(n > 0 && bc_decode(buf, (size_t)n, &m) == 0 && m.type == BC_MSG_NODE_STATUS);
	CHECK(m.u.status.sweeps == 42 && m.u.status.pods == 3 && m.u.status.drift_ppm == -7 &&
	      m.u.status.batt_mv == 12810 && strcmp(m.u.status.pps, "holdover") == 0);

	n = bc_encode_get_status_call(buf, sizeof(buf), 99);
	CHECK(n > 0 && bc_decode(buf, (size_t)n, &m) == 0 && m.type == BC_MSG_GET_STATUS_CALL && m.msg_id == 99);

	n = bc_encode_get_status_response(buf, sizeof(buf), 99, &st);
	CHECK(n > 0 && bc_decode(buf, (size_t)n, &m) == 0 && m.type == BC_MSG_GET_STATUS_RESPONSE);
	CHECK(m.msg_id == 99 && !m.error && m.u.status.batt_mv == 12810);
}

static void test_set_alert_cadence_pods_armed_embedding(void)
{
	uint8_t buf[128];
	struct bc_msg m;

	CHECK(bc_encode_set_alert(buf, sizeof(buf), 2, 600) > 0);
	CHECK(bc_decode(buf, (size_t)bc_encode_set_alert(buf, sizeof(buf), 2, 600), &m) == 0);
	CHECK(m.type == BC_MSG_SET_ALERT && m.u.set_alert.level == 2 && m.u.set_alert.seconds == 600);

	int n = bc_encode_set_cadence(buf, sizeof(buf), 300);
	CHECK(bc_decode(buf, (size_t)n, &m) == 0 && m.type == BC_MSG_SET_CADENCE && m.u.set_cadence.seconds == 300);

	uint8_t addrs[] = {1, 4, 5};
	n = bc_encode_set_pods(buf, sizeof(buf), addrs, 3);
	CHECK(bc_decode(buf, (size_t)n, &m) == 0 && m.type == BC_MSG_SET_PODS && m.u.set_pods.n_addrs == 3);
	CHECK(memcmp(m.u.set_pods.addrs, addrs, 3) == 0);

	n = bc_encode_set_armed(buf, sizeof(buf), true);
	CHECK(bc_decode(buf, (size_t)n, &m) == 0 && m.type == BC_MSG_SET_ARMED && m.u.set_armed.armed == true);

	uint8_t emb[16];
	for (int i = 0; i < 16; i++) emb[i] = (uint8_t)(i * 7);
	n = bc_encode_set_embedding(buf, sizeof(buf), emb, sizeof(emb));
	CHECK(bc_decode(buf, (size_t)n, &m) == 0 && m.type == BC_MSG_SET_EMBEDDING);
	CHECK(m.u.set_embedding.emb_len == 16 && memcmp(m.u.set_embedding.emb, emb, 16) == 0);
}

static void test_unknown_method_is_none_not_error(void)
{
	uint8_t buf[64];
	size_t off = 0;

	mp_put_array_header(buf, sizeof(buf), &off, 3);
	mp_put_int(buf, sizeof(buf), &off, BC_MSGTYPE_NOTIFICATION);
	mp_put_str(buf, sizeof(buf), &off, "future_method", 13);
	mp_put_array_header(buf, sizeof(buf), &off, 2);
	mp_put_int(buf, sizeof(buf), &off, 1);
	mp_put_int(buf, sizeof(buf), &off, 2);
	struct bc_msg m;

	CHECK(bc_decode(buf, off, &m) == 0 && m.type == BC_MSG_NONE);
}

static void test_error_response_is_flagged(void)
{
	uint8_t buf[64];
	size_t off = 0;

	mp_put_array_header(buf, sizeof(buf), &off, 4);
	mp_put_int(buf, sizeof(buf), &off, BC_MSGTYPE_RESPONSE);
	mp_put_int(buf, sizeof(buf), &off, 7);
	mp_put_array_header(buf, sizeof(buf), &off, 2); /* [code, message] error */
	mp_put_int(buf, sizeof(buf), &off, 0xFE);
	mp_put_str(buf, sizeof(buf), &off, "method not found", 16);
	mp_put_nil(buf, sizeof(buf), &off); /* result */
	struct bc_msg m;

	CHECK(bc_decode(buf, off, &m) == 0 && m.type == BC_MSG_GET_STATUS_RESPONSE && m.error && m.msg_id == 7);
}

/* ---------------------------------------------------------------- interop dump
 * Writes one encoded message per line (hex) to stdout, prefixed by a tag, so
 * test_interop.py can decode each with the real `msgpack` package and check
 * the logical content — the actual claim under test is "this C code speaks
 * real MessagePack-RPC", not just "this C code agrees with itself".
 */
static void hexdump(const char *tag, const uint8_t *buf, size_t len)
{
	printf("%s ", tag);
	for (size_t i = 0; i < len; i++) {
		printf("%02x", buf[i]);
	}
	printf("\n");
}

static void dump_interop_vectors(void)
{
	uint8_t buf[256];
	int n;

	n = bc_encode_pod_report(buf, sizeof(buf), 5, (const uint8_t *)"\x01\x02\x03", 3);
	hexdump("pod_report", buf, (size_t)n);
	n = bc_encode_pod_missing(buf, sizeof(buf), 4);
	hexdump("pod_missing", buf, (size_t)n);
	n = bc_encode_sweep_done(buf, sizeof(buf), 12, 2, 3);
	hexdump("sweep_done", buf, (size_t)n);
	n = bc_encode_neighbour_packet(buf, sizeof(buf), (const uint8_t *)"\xb7\x01\x02\x03", 4);
	hexdump("neighbour_packet", buf, (size_t)n);
	n = bc_encode_time_sync(buf, sizeof(buf), 1757400000123ULL, "locked");
	hexdump("time_sync", buf, (size_t)n);
	struct bc_status st = {.sweeps = 42, .pods = 3, .drift_ppm = -7, .batt_mv = 12810};
	strcpy(st.pps, "holdover");
	n = bc_encode_node_status(buf, sizeof(buf), &st);
	hexdump("node_status", buf, (size_t)n);
	n = bc_encode_get_status_call(buf, sizeof(buf), 99);
	hexdump("get_status_call", buf, (size_t)n);
	n = bc_encode_get_status_response(buf, sizeof(buf), 99, &st);
	hexdump("get_status_response", buf, (size_t)n);
	n = bc_encode_set_alert(buf, sizeof(buf), 2, 600);
	hexdump("set_alert", buf, (size_t)n);
	n = bc_encode_set_cadence(buf, sizeof(buf), 300);
	hexdump("set_cadence", buf, (size_t)n);
	uint8_t addrs[] = {1, 4, 5};
	n = bc_encode_set_pods(buf, sizeof(buf), addrs, 3);
	hexdump("set_pods", buf, (size_t)n);
	n = bc_encode_set_armed(buf, sizeof(buf), true);
	hexdump("set_armed", buf, (size_t)n);
	uint8_t emb[16];
	for (int i = 0; i < 16; i++) emb[i] = (uint8_t)(i * 7);
	n = bc_encode_set_embedding(buf, sizeof(buf), emb, sizeof(emb));
	hexdump("set_embedding", buf, (size_t)n);
}

int main(int argc, char **argv)
{
	if (argc > 1 && strcmp(argv[1], "--dump-interop") == 0) {
		dump_interop_vectors();
		return 0;
	}
	test_int_roundtrip();
	test_str_fixstr_str8_boundary();
	test_bin_bin8_bin16_boundary();
	test_array_fixarray_array16_boundary();
	test_scan_needs_more_then_ok();
	test_scan_rejects_unsupported_type();
	test_pod_report_roundtrip();
	test_sweep_done_roundtrip();
	test_time_sync_roundtrip();
	test_node_status_and_get_status_roundtrip();
	test_set_alert_cadence_pods_armed_embedding();
	test_unknown_method_is_none_not_error();
	test_error_response_is_flagged();
	if (failures) {
		fprintf(stderr, "%d FAILURES\n", failures);
		return 1;
	}
	printf("all native mprpc/bridge_contract tests passed\n");
	return 0;
}
