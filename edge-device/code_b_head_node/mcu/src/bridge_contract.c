/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>
#include <hn/bridge_contract.h>
#include <hn/mprpc.h>

/* ================================================================ shared helpers */
static int put_method_str(uint8_t *buf, size_t cap, size_t *off, const char *m)
{
	return mp_put_str(buf, cap, off, m, strlen(m));
}

/* [NOTIFICATION_TAG, method, params...] with params encoded by the caller
 * after this header — n_params is the count the caller is about to write. */
static int notify_header(uint8_t *buf, size_t cap, size_t *off, const char *method, size_t n_params)
{
	if (mp_put_array_header(buf, cap, off, 3)) {
		return -1;
	}
	if (mp_put_int(buf, cap, off, BC_MSGTYPE_NOTIFICATION)) {
		return -1;
	}
	if (put_method_str(buf, cap, off, method)) {
		return -1;
	}
	return mp_put_array_header(buf, cap, off, n_params);
}

static int encode_status_map(uint8_t *buf, size_t cap, size_t *off, const struct bc_status *st)
{
	return mp_put_map_header(buf, cap, off, 5) ||
	       mp_put_str(buf, cap, off, "sweeps", 6) || mp_put_int(buf, cap, off, st->sweeps) ||
	       mp_put_str(buf, cap, off, "pods", 4) || mp_put_int(buf, cap, off, st->pods) ||
	       mp_put_str(buf, cap, off, "pps", 3) || mp_put_str(buf, cap, off, st->pps, strlen(st->pps)) ||
	       mp_put_str(buf, cap, off, "drift_ppm", 9) || mp_put_int(buf, cap, off, st->drift_ppm) ||
	       mp_put_str(buf, cap, off, "batt_mv", 7) || mp_put_int(buf, cap, off, st->batt_mv);
}

/* ================================================================ MCU -> Linux */
int bc_encode_pod_report(uint8_t *buf, size_t cap, uint8_t addr, const uint8_t *report, size_t rlen)
{
	size_t off = 0;

	if (rlen > BC_MAX_REPORT) {
		return -1;
	}
	if (notify_header(buf, cap, &off, BC_METHOD_POD_REPORT, 2)) {
		return -1;
	}
	if (mp_put_int(buf, cap, &off, addr) || mp_put_bin(buf, cap, &off, report, rlen)) {
		return -1;
	}
	return (int)off;
}

int bc_encode_pod_missing(uint8_t *buf, size_t cap, uint8_t addr)
{
	size_t off = 0;

	if (notify_header(buf, cap, &off, BC_METHOD_POD_MISSING, 1)) {
		return -1;
	}
	return mp_put_int(buf, cap, &off, addr) ? -1 : (int)off;
}

int bc_encode_sweep_done(uint8_t *buf, size_t cap, uint32_t sweep, uint32_t present, uint32_t total)
{
	size_t off = 0;

	if (notify_header(buf, cap, &off, BC_METHOD_SWEEP_DONE, 3)) {
		return -1;
	}
	if (mp_put_int(buf, cap, &off, sweep) || mp_put_int(buf, cap, &off, present) ||
	    mp_put_int(buf, cap, &off, total)) {
		return -1;
	}
	return (int)off;
}

int bc_encode_neighbour_packet(uint8_t *buf, size_t cap, const uint8_t *packet, size_t plen)
{
	size_t off = 0;

	if (notify_header(buf, cap, &off, BC_METHOD_NEIGHBOUR_PACKET, 1)) {
		return -1;
	}
	return mp_put_bin(buf, cap, &off, packet, plen) ? -1 : (int)off;
}

int bc_encode_time_sync(uint8_t *buf, size_t cap, uint64_t utc_ms, const char *state)
{
	size_t off = 0;

	if (notify_header(buf, cap, &off, BC_METHOD_TIME_SYNC, 2)) {
		return -1;
	}
	if (mp_put_int(buf, cap, &off, (int64_t)utc_ms) || mp_put_str(buf, cap, &off, state, strlen(state))) {
		return -1;
	}
	return (int)off;
}

int bc_encode_node_status(uint8_t *buf, size_t cap, const struct bc_status *st)
{
	size_t off = 0;

	if (notify_header(buf, cap, &off, BC_METHOD_NODE_STATUS, 1)) {
		return -1;
	}
	return encode_status_map(buf, cap, &off, st) ? -1 : (int)off;
}

int bc_encode_get_status_response(uint8_t *buf, size_t cap, uint32_t msg_id, const struct bc_status *st)
{
	size_t off = 0;

	if (mp_put_array_header(buf, cap, &off, 4) || mp_put_int(buf, cap, &off, BC_MSGTYPE_RESPONSE) ||
	    mp_put_int(buf, cap, &off, msg_id) || mp_put_nil(buf, cap, &off) /* no error */) {
		return -1;
	}
	return encode_status_map(buf, cap, &off, st) ? -1 : (int)off;
}

/* ================================================================ Linux -> MCU */
int bc_encode_set_alert(uint8_t *buf, size_t cap, uint8_t level, uint32_t seconds)
{
	size_t off = 0;

	if (notify_header(buf, cap, &off, BC_METHOD_SET_ALERT, 2)) {
		return -1;
	}
	if (mp_put_int(buf, cap, &off, level) || mp_put_int(buf, cap, &off, seconds)) {
		return -1;
	}
	return (int)off;
}

int bc_encode_set_cadence(uint8_t *buf, size_t cap, uint32_t seconds)
{
	size_t off = 0;

	if (notify_header(buf, cap, &off, BC_METHOD_SET_CADENCE, 1)) {
		return -1;
	}
	return mp_put_int(buf, cap, &off, seconds) ? -1 : (int)off;
}

int bc_encode_set_pods(uint8_t *buf, size_t cap, const uint8_t *addrs, size_t n_addrs)
{
	size_t off = 0;

	if (notify_header(buf, cap, &off, BC_METHOD_SET_PODS, 1)) {
		return -1;
	}
	if (mp_put_array_header(buf, cap, &off, n_addrs)) {
		return -1;
	}
	for (size_t i = 0; i < n_addrs; i++) {
		if (mp_put_int(buf, cap, &off, addrs[i])) {
			return -1;
		}
	}
	return (int)off;
}

int bc_encode_set_armed(uint8_t *buf, size_t cap, bool armed)
{
	size_t off = 0;

	if (notify_header(buf, cap, &off, BC_METHOD_SET_ARMED, 1)) {
		return -1;
	}
	return mp_put_bool(buf, cap, &off, armed) ? -1 : (int)off;
}

int bc_encode_set_embedding(uint8_t *buf, size_t cap, const uint8_t *emb, size_t elen)
{
	size_t off = 0;

	if (notify_header(buf, cap, &off, BC_METHOD_SET_EMBEDDING, 1)) {
		return -1;
	}
	return mp_put_bin(buf, cap, &off, emb, elen) ? -1 : (int)off;
}

int bc_encode_get_status_call(uint8_t *buf, size_t cap, uint32_t msg_id)
{
	size_t off = 0;

	if (mp_put_array_header(buf, cap, &off, 4) || mp_put_int(buf, cap, &off, BC_MSGTYPE_REQUEST) ||
	    mp_put_int(buf, cap, &off, msg_id) || put_method_str(buf, cap, &off, BC_METHOD_GET_STATUS)) {
		return -1;
	}
	return mp_put_array_header(buf, cap, &off, 0) ? -1 : (int)off;
}

/* ================================================================ decoder */
static int method_eq(const char *ptr, size_t len, const char *name)
{
	return len == strlen(name) && memcmp(ptr, name, len) == 0;
}

static int decode_status_map(struct mp_reader *r, struct bc_status *st)
{
	size_t n;
	const char *kp;
	size_t klen;
	int64_t iv;

	memset(st, 0, sizeof(*st));
	if (mp_get_map_header(r, &n)) {
		return -1;
	}
	for (size_t i = 0; i < n; i++) {
		if (mp_get_str(r, &kp, &klen)) {
			return -1;
		}
		if (method_eq(kp, klen, "sweeps")) {
			if (mp_get_int(r, &iv)) return -1;
			st->sweeps = (uint32_t)iv;
		} else if (method_eq(kp, klen, "pods")) {
			if (mp_get_int(r, &iv)) return -1;
			st->pods = (uint32_t)iv;
		} else if (method_eq(kp, klen, "pps")) {
			const char *sp;
			size_t slen;

			if (mp_get_str(r, &sp, &slen)) return -1;
			if (slen > BC_MAX_STR) slen = BC_MAX_STR;
			memcpy(st->pps, sp, slen);
			st->pps[slen] = '\0';
		} else if (method_eq(kp, klen, "drift_ppm")) {
			if (mp_get_int(r, &iv)) return -1;
			st->drift_ppm = (int32_t)iv;
		} else if (method_eq(kp, klen, "batt_mv")) {
			if (mp_get_int(r, &iv)) return -1;
			st->batt_mv = (uint32_t)iv;
		} else if (mp_skip_value(r)) {
			return -1; /* forward-compatible: unknown key, skip its value */
		}
	}
	return 0;
}

static int decode_notification_params(struct mp_reader *r, const char *method, size_t mlen, struct bc_msg *out)
{
	size_t n;
	int64_t iv;

	if (mp_get_array_header(r, &n)) {
		return -1;
	}
	if (method_eq(method, mlen, BC_METHOD_POD_REPORT) && n == 2) {
		const uint8_t *rep;
		size_t rlen;

		if (mp_get_int(r, &iv) || mp_get_bin(r, &rep, &rlen) || rlen > BC_MAX_REPORT) {
			return -1;
		}
		out->type = BC_MSG_POD_REPORT;
		out->u.pod_report.addr = (uint8_t)iv;
		out->u.pod_report.report = rep;
		out->u.pod_report.report_len = rlen;
		return 0;
	}
	if (method_eq(method, mlen, BC_METHOD_POD_MISSING) && n == 1) {
		if (mp_get_int(r, &iv)) return -1;
		out->type = BC_MSG_POD_MISSING;
		out->u.pod_missing.addr = (uint8_t)iv;
		return 0;
	}
	if (method_eq(method, mlen, BC_METHOD_SWEEP_DONE) && n == 3) {
		int64_t a, b, c;

		if (mp_get_int(r, &a) || mp_get_int(r, &b) || mp_get_int(r, &c)) return -1;
		out->type = BC_MSG_SWEEP_DONE;
		out->u.sweep_done.sweep = (uint32_t)a;
		out->u.sweep_done.present = (uint32_t)b;
		out->u.sweep_done.total = (uint32_t)c;
		return 0;
	}
	if (method_eq(method, mlen, BC_METHOD_NEIGHBOUR_PACKET) && n == 1) {
		const uint8_t *pkt;
		size_t plen;

		if (mp_get_bin(r, &pkt, &plen)) return -1;
		out->type = BC_MSG_NEIGHBOUR_PACKET;
		out->u.neighbour_packet.packet = pkt;
		out->u.neighbour_packet.packet_len = plen;
		return 0;
	}
	if (method_eq(method, mlen, BC_METHOD_TIME_SYNC) && n == 2) {
		const char *sp;
		size_t slen;

		if (mp_get_int(r, &iv) || mp_get_str(r, &sp, &slen)) return -1;
		if (slen > BC_MAX_STR) slen = BC_MAX_STR;
		out->type = BC_MSG_TIME_SYNC;
		out->u.time_sync.utc_ms = (uint64_t)iv;
		memcpy(out->u.time_sync.state, sp, slen);
		out->u.time_sync.state[slen] = '\0';
		return 0;
	}
	if (method_eq(method, mlen, BC_METHOD_NODE_STATUS) && n == 1) {
		if (decode_status_map(r, &out->u.status)) return -1;
		out->type = BC_MSG_NODE_STATUS;
		return 0;
	}
	if (method_eq(method, mlen, BC_METHOD_SET_ALERT) && n == 2) {
		int64_t lvl, sec;

		if (mp_get_int(r, &lvl) || mp_get_int(r, &sec)) return -1;
		out->type = BC_MSG_SET_ALERT;
		out->u.set_alert.level = (uint8_t)lvl;
		out->u.set_alert.seconds = (uint32_t)sec;
		return 0;
	}
	if (method_eq(method, mlen, BC_METHOD_SET_CADENCE) && n == 1) {
		if (mp_get_int(r, &iv)) return -1;
		out->type = BC_MSG_SET_CADENCE;
		out->u.set_cadence.seconds = (uint32_t)iv;
		return 0;
	}
	if (method_eq(method, mlen, BC_METHOD_SET_PODS) && n == 1) {
		size_t na;

		if (mp_get_array_header(r, &na) || na > BC_MAX_ADDRS) return -1;
		out->type = BC_MSG_SET_PODS;
		out->u.set_pods.n_addrs = (uint8_t)na;
		for (size_t i = 0; i < na; i++) {
			if (mp_get_int(r, &iv)) return -1;
			out->u.set_pods.addrs[i] = (uint8_t)iv;
		}
		return 0;
	}
	if (method_eq(method, mlen, BC_METHOD_SET_ARMED) && n == 1) {
		bool armed;

		if (mp_get_bool(r, &armed)) return -1;
		out->type = BC_MSG_SET_ARMED;
		out->u.set_armed.armed = armed;
		return 0;
	}
	if (method_eq(method, mlen, BC_METHOD_SET_EMBEDDING) && n == 1) {
		const uint8_t *emb;
		size_t elen;

		if (mp_get_bin(r, &emb, &elen) || elen > BC_MAX_EMB) return -1;
		out->type = BC_MSG_SET_EMBEDDING;
		out->u.set_embedding.emb_len = (uint8_t)elen;
		memcpy(out->u.set_embedding.emb, emb, elen);
		return 0;
	}
	/* Recognised MessagePack-RPC, unrecognised method/arity: skip its params and report NONE. */
	for (size_t i = 0; i < n; i++) {
		if (mp_skip_value(r)) {
			return -1;
		}
	}
	out->type = BC_MSG_NONE;
	return 0;
}

int bc_decode(const uint8_t *buf, size_t len, struct bc_msg *out)
{
	struct mp_reader r;
	size_t arr_n;
	int64_t type_tag;

	memset(out, 0, sizeof(*out));
	mp_reader_init(&r, buf, len);
	if (mp_get_array_header(&r, &arr_n) || mp_get_int(&r, &type_tag)) {
		return -1;
	}
	if (type_tag == BC_MSGTYPE_NOTIFICATION && arr_n == 3) {
		const char *method;
		size_t mlen;

		if (mp_get_str(&r, &method, &mlen)) {
			return -1;
		}
		return decode_notification_params(&r, method, mlen, out);
	}
	if (type_tag == BC_MSGTYPE_REQUEST && arr_n == 4) {
		int64_t msgid;
		const char *method;
		size_t mlen, pn;

		if (mp_get_int(&r, &msgid) || mp_get_str(&r, &method, &mlen)) {
			return -1;
		}
		if (mp_get_array_header(&r, &pn)) {
			return -1;
		}
		for (size_t i = 0; i < pn; i++) {
			if (mp_skip_value(&r)) {
				return -1;
			}
		}
		if (method_eq(method, mlen, BC_METHOD_GET_STATUS) && pn == 0) {
			out->type = BC_MSG_GET_STATUS_CALL;
			out->msg_id = (uint32_t)msgid;
			return 0;
		}
		out->type = BC_MSG_NONE;
		out->msg_id = (uint32_t)msgid;
		return 0;
	}
	if (type_tag == BC_MSGTYPE_RESPONSE && arr_n == 4) {
		int64_t msgid;

		if (mp_get_int(&r, &msgid)) {
			return -1;
		}
		out->msg_id = (uint32_t)msgid;
		if (mp_get_nil(&r)) {
			if (decode_status_map(&r, &out->u.status)) {
				return -1;
			}
			out->type = BC_MSG_GET_STATUS_RESPONSE;
			out->error = false;
			return 0;
		}
		/* error present: [code, message] — skip it, skip the (irrelevant) result, report the error */
		if (mp_skip_value(&r) || mp_skip_value(&r)) {
			return -1;
		}
		out->type = BC_MSG_GET_STATUS_RESPONSE;
		out->error = true;
		return 0;
	}
	return -1;
}
