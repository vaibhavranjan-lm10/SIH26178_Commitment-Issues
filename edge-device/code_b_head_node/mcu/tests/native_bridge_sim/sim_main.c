/*
 * TEST-ONLY host simulator of the MCU side's bridge_contract endpoint.
 *
 * NOT firmware. It links the exact same code_b_head_node/mcu/src/mprpc.c
 * and src/bridge_contract.c the real Zephyr image ships (see
 * ../../CMakeLists.txt) — only the transport is swapped: a plain POSIX
 * TCP client socket standing in for the UART, connecting to a Router
 * (real or, for this test, the MockRouter in code_b_head_node/linux/tests/)
 * at the officially-supported tcp://host:port development address the
 * Python arduino_router_bridge package documents (unix:// is for the real
 * board only). This is how the integration test exercises the actual
 * wire-format code without a physical board — see
 * code_b_head_node/README.md "How the integration test runs".
 *
 * Registers the six methods the MCU side provides (set_alert, set_cadence,
 * set_pods, set_armed, set_embedding, get_status), then services two
 * inputs: bytes from the router socket (real bridge traffic, dispatched
 * through bc_decode/handle exactly as main.c's poll_bridge would), and
 * line commands on stdin (test control — never part of the real contract):
 *
 *   POD <addr> <hex>            -> emits pod_report
 *   MISSING <addr>               -> emits pod_missing
 *   SWEEP <sweep> <present> <total> -> emits sweep_done
 *   NBR <hex>                    -> emits neighbour_packet
 *   TIME <utc_ms> <state>        -> emits time_sync
 *   SETPPS <state>                -> sets the pps field NODESTATUS/get_status report next
 *   NODESTATUS                   -> emits node_status (current sim state)
 *   DUMP                         -> prints "STATE ..." with everything the
 *                                    sim has received from the Linux side so
 *                                    far, so the test can assert on it
 *   QUIT                         -> exit
 * SPDX-License-Identifier: Apache-2.0
 */
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <hn/bridge_contract.h>
#include <hn/mprpc.h>

#define REQUEST 0
#define RESPONSE 1
#define NOTIFICATION 2

static int sock = -1;
static uint32_t next_msg_id = 1;

/* ---- sim state, mutated by messages received FROM the Linux side ---- */
static struct {
	uint8_t alert_level;
	uint32_t alert_seconds;
	uint32_t cadence_seconds;
	uint8_t pods[BC_MAX_ADDRS];
	uint8_t n_pods;
	bool armed;
	uint8_t emb[BC_MAX_EMB];
	uint8_t emb_len;
	uint32_t registrations_acked;
} state;

static struct bc_status status = {.sweeps = 0, .pods = 3, .drift_ppm = 0, .batt_mv = 12800};

static void send_all(const uint8_t *buf, size_t len)
{
	size_t off = 0;

	while (off < len) {
		ssize_t n = send(sock, buf + off, len - off, 0);

		if (n <= 0) {
			perror("send");
			exit(1);
		}
		off += (size_t)n;
	}
}

/* $/register as a REQUEST, per the real protocol (Bridge._register calls
 * self.call("$/register", method_name)) — blocks for the RESPONSE before
 * returning, exactly like the vendor's provide(). */
static void register_method(const char *name)
{
	uint8_t buf[64];
	size_t off = 0;
	uint32_t id = next_msg_id++;

	static const char register_method_name[] = "$/register";

	mp_put_array_header(buf, sizeof(buf), &off, 4);
	mp_put_int(buf, sizeof(buf), &off, REQUEST);
	mp_put_int(buf, sizeof(buf), &off, id);
	mp_put_str(buf, sizeof(buf), &off, register_method_name, strlen(register_method_name));
	mp_put_array_header(buf, sizeof(buf), &off, 1);
	mp_put_str(buf, sizeof(buf), &off, name, strlen(name));
	send_all(buf, off);

	/* Blocking read until this exact response arrives (registration is
	 * strictly sequential at startup; nothing else is in flight yet). */
	uint8_t rx[256];
	size_t rx_len = 0;

	for (;;) {
		ssize_t n = recv(sock, rx + rx_len, sizeof(rx) - rx_len, 0);

		if (n <= 0) {
			fprintf(stderr, "register %s: connection closed\n", name);
			exit(1);
		}
		rx_len += (size_t)n;
		size_t msg_len;

		if (mp_scan_value(rx, rx_len, &msg_len) != MP_SCAN_OK) {
			continue;
		}
		struct mp_reader r;
		size_t arr_n;
		int64_t type_tag, msgid;

		mp_reader_init(&r, rx, msg_len);
		mp_get_array_header(&r, &arr_n);
		mp_get_int(&r, &type_tag);
		if (type_tag == RESPONSE && arr_n == 4) {
			mp_get_int(&r, &msgid);
			if ((uint32_t)msgid == id) {
				bool is_error = !mp_get_nil(&r);

				if (is_error) {
					fprintf(stderr, "registering %s FAILED (router refused)\n", name);
					exit(1);
				}
				fprintf(stderr, "registered %s: ok\n", name);
				state.registrations_acked++;
				return;
			}
		}
		/* Not our response (shouldn't happen this early) — drop and keep reading. */
		rx_len = 0;
	}
}

static void handle_message(const uint8_t *buf, size_t len)
{
	struct bc_msg m;

	if (bc_decode(buf, len, &m) != 0) {
		fprintf(stderr, "malformed message ignored\n");
		return;
	}
	uint8_t out[128];

	switch (m.type) {
	case BC_MSG_SET_ALERT:
		state.alert_level = m.u.set_alert.level;
		state.alert_seconds = m.u.set_alert.seconds;
		break;
	case BC_MSG_SET_CADENCE:
		state.cadence_seconds = m.u.set_cadence.seconds;
		break;
	case BC_MSG_SET_PODS:
		state.n_pods = m.u.set_pods.n_addrs;
		memcpy(state.pods, m.u.set_pods.addrs, m.u.set_pods.n_addrs);
		break;
	case BC_MSG_SET_ARMED:
		state.armed = m.u.set_armed.armed;
		break;
	case BC_MSG_SET_EMBEDDING:
		state.emb_len = m.u.set_embedding.emb_len;
		memcpy(state.emb, m.u.set_embedding.emb, m.u.set_embedding.emb_len);
		break;
	case BC_MSG_GET_STATUS_CALL:
		send_all(out, (size_t)bc_encode_get_status_response(out, sizeof(out), m.msg_id, &status));
		break;
	default:
		break;
	}
}

static void on_socket_readable(void)
{
	static uint8_t acc[2048];
	static size_t acc_len;
	uint8_t chunk[512];
	ssize_t n = recv(sock, chunk, sizeof(chunk), 0);

	if (n <= 0) {
		fprintf(stderr, "router connection closed\n");
		exit(n == 0 ? 0 : 1);
	}
	if (acc_len + (size_t)n > sizeof(acc)) {
		acc_len = 0; /* defensive: never expected in this test harness */
	}
	memcpy(acc + acc_len, chunk, (size_t)n);
	acc_len += (size_t)n;

	for (;;) {
		size_t msg_len;
		enum mp_scan_result r = mp_scan_value(acc, acc_len, &msg_len);

		if (r == MP_SCAN_NEED_MORE) {
			break;
		}
		if (r == MP_SCAN_ERROR) {
			memmove(acc, acc + 1, --acc_len);
			continue;
		}
		handle_message(acc, msg_len);
		memmove(acc, acc + msg_len, acc_len - msg_len);
		acc_len -= msg_len;
	}
}

static void hex_decode(const char *hex, uint8_t *out, size_t *len)
{
	size_t n = strlen(hex) / 2;

	for (size_t i = 0; i < n; i++) {
		unsigned b;

		sscanf(hex + 2 * i, "%2x", &b);
		out[i] = (uint8_t)b;
	}
	*len = n;
}

static void hex_encode(const uint8_t *in, size_t n, char *out)
{
	static const char d[] = "0123456789abcdef";

	for (size_t i = 0; i < n; i++) {
		out[2 * i] = d[in[i] >> 4];
		out[2 * i + 1] = d[in[i] & 0xf];
	}
	out[2 * n] = '\0';
}

static void on_stdin_line(char *line)
{
	uint8_t buf[256];
	char hexbuf[BC_MAX_EMB * 2 + 1];

	line[strcspn(line, "\r\n")] = '\0';
	if (strncmp(line, "POD ", 4) == 0) {
		unsigned addr;
		char hex[512];

		sscanf(line + 4, "%u %511s", &addr, hex);
		uint8_t report[256];
		size_t rlen;

		hex_decode(hex, report, &rlen);
		send_all(buf, (size_t)bc_encode_pod_report(buf, sizeof(buf), (uint8_t)addr, report, rlen));
	} else if (strncmp(line, "MISSING ", 8) == 0) {
		unsigned addr = (unsigned)atoi(line + 8);

		send_all(buf, (size_t)bc_encode_pod_missing(buf, sizeof(buf), (uint8_t)addr));
	} else if (strncmp(line, "SWEEP ", 6) == 0) {
		unsigned sw, pr, tot;

		sscanf(line + 6, "%u %u %u", &sw, &pr, &tot);
		status.sweeps = sw;
		/* Mirrors main.c's service_bus(): node_status rides along every
		 * sweep, not only on request. */
		send_all(buf, (size_t)bc_encode_sweep_done(buf, sizeof(buf), sw, pr, tot));
		send_all(buf, (size_t)bc_encode_node_status(buf, sizeof(buf), &status));
	} else if (strncmp(line, "NBR ", 4) == 0) {
		uint8_t pkt[256];
		size_t plen;

		hex_decode(line + 4, pkt, &plen);
		send_all(buf, (size_t)bc_encode_neighbour_packet(buf, sizeof(buf), pkt, plen));
	} else if (strncmp(line, "TIME ", 5) == 0) {
		unsigned long long utc_ms;
		char st[16];

		sscanf(line + 5, "%llu %15s", &utc_ms, st);
		send_all(buf, (size_t)bc_encode_time_sync(buf, sizeof(buf), utc_ms, st));
	} else if (strncmp(line, "SETPPS ", 7) == 0) {
		strncpy(status.pps, line + 7, BC_MAX_STR);
		status.pps[BC_MAX_STR] = '\0';
	} else if (strcmp(line, "NODESTATUS") == 0) {
		send_all(buf, (size_t)bc_encode_node_status(buf, sizeof(buf), &status));
	} else if (strcmp(line, "DUMP") == 0) {
		hex_encode(state.emb, state.emb_len, hexbuf);
		printf("STATE alert_level=%u alert_seconds=%u cadence=%u armed=%d n_pods=%u pods=",
		       state.alert_level, state.alert_seconds, state.cadence_seconds, state.armed, state.n_pods);
		for (uint8_t i = 0; i < state.n_pods; i++) {
			printf("%u%s", state.pods[i], i + 1 < state.n_pods ? "," : "");
		}
		printf(" emb=%s registrations_acked=%u\n", hexbuf, state.registrations_acked);
		fflush(stdout);
	} else if (strcmp(line, "QUIT") == 0) {
		exit(0);
	} else if (line[0] != '\0') {
		fprintf(stderr, "sim: unknown command %s\n", line);
	}
}

int main(int argc, char **argv)
{
	if (argc != 3) {
		fprintf(stderr, "usage: sim_main <host> <port>\n");
		return 2;
	}
	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		perror("socket");
		return 1;
	}
	int one = 1;

	setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
	struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons((uint16_t)atoi(argv[2]))};

	if (inet_pton(AF_INET, argv[1], &addr.sin_addr) != 1 ||
	    connect(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		perror("connect");
		return 1;
	}
	strcpy(status.pps, "unlocked");

	register_method(BC_METHOD_SET_ALERT);
	register_method(BC_METHOD_SET_CADENCE);
	register_method(BC_METHOD_SET_PODS);
	register_method(BC_METHOD_SET_ARMED);
	register_method(BC_METHOD_SET_EMBEDDING);
	register_method(BC_METHOD_GET_STATUS);
	printf("READY\n");
	fflush(stdout);

	/* Raw reads for stdin too, not fgets(): select() reports the
	 * underlying fd readable, but buffered stdio can slurp several
	 * already-queued lines into its own buffer on a single read(),
	 * leaving the fd with nothing left for select() to see even though
	 * more complete lines are sitting unprocessed in libc's buffer. */
	static char stdin_acc[4096];
	static size_t stdin_len;

	for (;;) {
		fd_set fds;

		FD_ZERO(&fds);
		FD_SET(sock, &fds);
		FD_SET(STDIN_FILENO, &fds);
		int maxfd = sock > STDIN_FILENO ? sock : STDIN_FILENO;

		if (select(maxfd + 1, &fds, NULL, NULL, NULL) < 0) {
			if (errno == EINTR) {
				continue;
			}
			perror("select");
			return 1;
		}
		if (FD_ISSET(sock, &fds)) {
			on_socket_readable();
		}
		if (FD_ISSET(STDIN_FILENO, &fds)) {
			ssize_t n = read(STDIN_FILENO, stdin_acc + stdin_len, sizeof(stdin_acc) - stdin_len - 1);

			if (n <= 0) {
				return 0; /* EOF or error: test harness is done with us */
			}
			stdin_len += (size_t)n;
			char *nl;

			while ((nl = memchr(stdin_acc, '\n', stdin_len)) != NULL) {
				size_t linelen = (size_t)(nl - stdin_acc);

				stdin_acc[linelen] = '\0';
				on_stdin_line(stdin_acc);
				size_t consumed = linelen + 1;

				memmove(stdin_acc, stdin_acc + consumed, stdin_len - consumed);
				stdin_len -= consumed;
			}
		}
	}
}
