/*
 * bridge_contract — the actual message contract between Code B's MCU side
 * and Linux side, as it would ride the UNO Q's native MsgPack-RPC bridge.
 * Full rationale and message-by-message documentation lives in
 * code_b_head_node/README.md ("The MCU <-> Linux message contract") —
 * this header is the source of truth for the wire shapes, that file
 * explains *why*.
 *
 * Every message is a MessagePack-RPC NOTIFICATION ([2, method, params]),
 * one direction each, except "get_status" which is a CALL/RESPONSE pair
 * (Linux calls, MCU responds) because a duty-cycle poll wants a fresh
 * synchronous answer rather than waiting for the next periodic push.
 *
 *   MCU -> Linux (sensor readings and pod health):
 *     pod_report(addr, report_bin)         one pod's decoded wire report
 *     pod_missing(addr)                    silent pod this sweep
 *     sweep_done(sweep, present, total)    one RS-485 sweep finished — the
 *                                          natural trigger for one inference cycle
 *     neighbour_packet(packet_bin)         a raw LoRa mesh packet from a peer node
 *     time_sync(utc_ms, state)             GNSS-disciplined time, state = "locked" |
 *                                          "holdover" | "unlocked"
 *     node_status(map)                     periodic health: sweeps, pods, pps, drift_ppm, batt_mv
 *
 *   Linux -> MCU (alert-trigger and duty-cycle-change commands):
 *     set_alert(level, seconds)            0=off/1=advisory/2=warning; siren pattern (§6.8)
 *     set_cadence(seconds)                 risk-adaptive duty cycle (§6.4)
 *     set_pods(addrs)                      RS-485 poll list
 *     set_armed(armed)                     MOX heaters on all pods
 *     set_embedding(emb_bin)               this node's embedding for the next mesh slot
 *
 *   Linux -> MCU, MCU -> Linux (call/response):
 *     get_status() -> map                  same shape as node_status, pulled on demand
 *
 * Pure: encoders fill a caller-provided buffer and return a length; the
 * decoder classifies one already-length-known message (mp_scan_value has
 * already found its boundary) into a tagged union. No I/O here — main.c
 * (firmware) and sim_main.c (host test harness) each supply their own
 * transport loop around these functions.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef HN_BRIDGE_CONTRACT_H_
#define HN_BRIDGE_CONTRACT_H_
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BC_MAX_ADDRS 16
#define BC_MAX_EMB 32
#define BC_MAX_REPORT 200
#define BC_MAX_STR 15

/* Method names — the one place they are spelled; both encode and decode
 * go through these, so a typo cannot make one side silently stop matching
 * the other's method name. */
#define BC_METHOD_POD_REPORT "pod_report"
#define BC_METHOD_POD_MISSING "pod_missing"
#define BC_METHOD_SWEEP_DONE "sweep_done"
#define BC_METHOD_NEIGHBOUR_PACKET "neighbour_packet"
#define BC_METHOD_TIME_SYNC "time_sync"
#define BC_METHOD_NODE_STATUS "node_status"
#define BC_METHOD_SET_ALERT "set_alert"
#define BC_METHOD_SET_CADENCE "set_cadence"
#define BC_METHOD_SET_PODS "set_pods"
#define BC_METHOD_SET_ARMED "set_armed"
#define BC_METHOD_SET_EMBEDDING "set_embedding"
#define BC_METHOD_GET_STATUS "get_status"

/* MessagePack-RPC message-type tags (protocol.py REQUEST/RESPONSE/NOTIFICATION). */
#define BC_MSGTYPE_REQUEST 0
#define BC_MSGTYPE_RESPONSE 1
#define BC_MSGTYPE_NOTIFICATION 2

/* node_status / get_status result fields. */
struct bc_status {
	uint32_t sweeps;
	uint32_t pods;
	char pps[BC_MAX_STR + 1]; /* "locked" | "holdover" | "unlocked" */
	int32_t drift_ppm;
	uint32_t batt_mv;
};

/* ---- encoders: MCU -> Linux notifications. Return byte length or -1 (ENOSPC). */
int bc_encode_pod_report(uint8_t *buf, size_t cap, uint8_t addr, const uint8_t *report, size_t rlen);
int bc_encode_pod_missing(uint8_t *buf, size_t cap, uint8_t addr);
int bc_encode_sweep_done(uint8_t *buf, size_t cap, uint32_t sweep, uint32_t present, uint32_t total);
int bc_encode_neighbour_packet(uint8_t *buf, size_t cap, const uint8_t *packet, size_t plen);
int bc_encode_time_sync(uint8_t *buf, size_t cap, uint64_t utc_ms, const char *state);
int bc_encode_node_status(uint8_t *buf, size_t cap, const struct bc_status *st);
/* get_status RESPONSE (MCU answering a Linux CALL): no-error result = the status map. */
int bc_encode_get_status_response(uint8_t *buf, size_t cap, uint32_t msg_id, const struct bc_status *st);

/* ---- encoders: Linux -> MCU. Same functions, reused by the Linux-side test
 * harness / MockRouter-adjacent tooling and by any host-side simulator; the
 * real Linux runtime encodes these via the official Python msgpack-rpc
 * bridge instead (see prahari_hn/bridge.py), not via this C module. */
int bc_encode_set_alert(uint8_t *buf, size_t cap, uint8_t level, uint32_t seconds);
int bc_encode_set_cadence(uint8_t *buf, size_t cap, uint32_t seconds);
int bc_encode_set_pods(uint8_t *buf, size_t cap, const uint8_t *addrs, size_t n_addrs);
int bc_encode_set_armed(uint8_t *buf, size_t cap, bool armed);
int bc_encode_set_embedding(uint8_t *buf, size_t cap, const uint8_t *emb, size_t elen);
int bc_encode_get_status_call(uint8_t *buf, size_t cap, uint32_t msg_id);

/* ---- decoder: classify one complete message (any direction) */
enum bc_msg_type {
	BC_MSG_NONE = 0,          /* not a message this contract recognises: ignore */
	BC_MSG_POD_REPORT,
	BC_MSG_POD_MISSING,
	BC_MSG_SWEEP_DONE,
	BC_MSG_NEIGHBOUR_PACKET,
	BC_MSG_TIME_SYNC,
	BC_MSG_NODE_STATUS,
	BC_MSG_SET_ALERT,
	BC_MSG_SET_CADENCE,
	BC_MSG_SET_PODS,
	BC_MSG_SET_ARMED,
	BC_MSG_SET_EMBEDDING,
	BC_MSG_GET_STATUS_CALL,     /* someone is calling us; msg_id must be echoed in the response */
	BC_MSG_GET_STATUS_RESPONSE, /* our earlier get_status call answered */
};

struct bc_msg {
	enum bc_msg_type type;
	uint32_t msg_id; /* valid for BC_MSG_GET_STATUS_CALL / _RESPONSE */
	bool error;      /* BC_MSG_GET_STATUS_RESPONSE only: peer answered with an RPC error */
	union {
		struct {
			uint8_t addr;
			const uint8_t *report;
			size_t report_len;
		} pod_report;
		struct {
			uint8_t addr;
		} pod_missing;
		struct {
			uint32_t sweep, present, total;
		} sweep_done;
		struct {
			const uint8_t *packet;
			size_t packet_len;
		} neighbour_packet;
		struct {
			uint64_t utc_ms;
			char state[BC_MAX_STR + 1];
		} time_sync;
		struct bc_status status; /* node_status and get_status_response share this shape */
		struct {
			uint8_t level;
			uint32_t seconds;
		} set_alert;
		struct {
			uint32_t seconds;
		} set_cadence;
		struct {
			uint8_t addrs[BC_MAX_ADDRS];
			uint8_t n_addrs;
		} set_pods;
		struct {
			bool armed;
		} set_armed;
		struct {
			uint8_t emb[BC_MAX_EMB];
			uint8_t emb_len;
		} set_embedding;
	} u;
};

/*
 * Decode a message previously bounded by mp_scan_value. The message-type
 * tag (request/response/notification) is self-describing — it is the
 * first element of the outer array — so no side information is needed.
 * Returns 0 on a recognised message (out->type set, BC_MSG_NONE if the
 * message parses as valid MessagePack-RPC but names a method/shape this
 * contract does not know), -1 if buf[0..len) is not even well-formed
 * MessagePack-RPC. For BC_MSG_GET_STATUS_RESPONSE, out->error is true if
 * the peer answered with an RPC error (out->u.status is then zeroed,
 * not a real reading) rather than a status map.
 */
int bc_decode(const uint8_t *buf, size_t len, struct bc_msg *out);
#endif /* HN_BRIDGE_CONTRACT_H_ */
