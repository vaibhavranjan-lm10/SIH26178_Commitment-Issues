/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <string.h>
#include <prahari/calib.h>
#include <prahari/params.h>
#include <prahari/pod_position.h> /* pod_config_crc8 */

static void put32(uint8_t *p, int32_t v)
{
	uint32_t u = (uint32_t)v;

	p[0] = (uint8_t)u;
	p[1] = (uint8_t)(u >> 8);
	p[2] = (uint8_t)(u >> 16);
	p[3] = (uint8_t)(u >> 24);
}

static int32_t get32(const uint8_t *p)
{
	return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
			 ((uint32_t)p[3] << 24));
}

int calib_table_encode(const struct calib_table *t, uint8_t *out, size_t len)
{
	size_t need;

	if (!t || !out || t->n > CALIB_MAX_ENTRIES) {
		return -EINVAL;
	}
	need = CALIB_HDR_LEN + (size_t)t->n * CALIB_ENTRY_LEN + 1;
	if (len < need) {
		return -ENOSPC;
	}
	out[0] = CALIB_MAGIC0;
	out[1] = CALIB_MAGIC1;
	out[2] = CALIB_VERSION;
	out[3] = t->n;
	for (uint8_t i = 0; i < t->n; i++) {
		uint8_t *e = out + CALIB_HDR_LEN + (size_t)i * CALIB_ENTRY_LEN;

		if (!prahari_param_is_insitu(t->e[i].param)) {
			return -EINVAL;
		}
		e[0] = t->e[i].param;
		put32(e + 1, t->e[i].raw_lo);
		put32(e + 5, t->e[i].ref_lo);
		put32(e + 9, t->e[i].raw_hi);
		put32(e + 13, t->e[i].ref_hi);
	}
	out[need - 1] = pod_config_crc8(out, need - 1);
	return (int)need;
}

int calib_table_decode(const uint8_t *blob, size_t len, struct calib_table *t)
{
	size_t need;
	uint8_t n;

	if (!t) {
		return -EINVAL;
	}
	memset(t, 0, sizeof(*t));
	if (!blob || len < CALIB_HDR_LEN + 1) {
		return -ENOENT;
	}
	if (blob[0] == 0xFF && blob[1] == 0xFF && blob[2] == 0xFF && blob[3] == 0xFF) {
		return -ENOENT; /* blank EEPROM */
	}
	if (blob[0] != CALIB_MAGIC0 || blob[1] != CALIB_MAGIC1 || blob[2] != CALIB_VERSION) {
		return -EINVAL;
	}
	n = blob[3];
	if (n > CALIB_MAX_ENTRIES) {
		return -EINVAL;
	}
	need = CALIB_HDR_LEN + (size_t)n * CALIB_ENTRY_LEN + 1;
	if (len < need || blob[need - 1] != pod_config_crc8(blob, need - 1)) {
		return -EINVAL;
	}
	for (uint8_t i = 0; i < n; i++) {
		const uint8_t *e = blob + CALIB_HDR_LEN + (size_t)i * CALIB_ENTRY_LEN;

		if (!prahari_param_is_insitu(e[0])) {
			memset(t, 0, sizeof(*t));
			return -EINVAL;
		}
		t->e[i].param = e[0];
		t->e[i].raw_lo = get32(e + 1);
		t->e[i].ref_lo = get32(e + 5);
		t->e[i].raw_hi = get32(e + 9);
		t->e[i].ref_hi = get32(e + 13);
	}
	t->n = n;
	return 0;
}

const struct calib_entry *calib_find(const struct calib_table *t, uint8_t param)
{
	if (!t) {
		return NULL;
	}
	for (uint8_t i = 0; i < t->n; i++) {
		if (t->e[i].param == param) {
			return &t->e[i];
		}
	}
	return NULL;
}

int calib_apply(const struct calib_entry *e, int32_t raw, int32_t *out)
{
	int64_t span_raw, span_ref, num, q;

	if (!e || !out || e->raw_hi == e->raw_lo) {
		return -EINVAL;
	}
	span_raw = (int64_t)e->raw_hi - e->raw_lo;
	span_ref = (int64_t)e->ref_hi - e->ref_lo;
	num = ((int64_t)raw - e->raw_lo) * span_ref;
	/* round half away from zero */
	if ((num >= 0) == (span_raw >= 0)) {
		q = (num + span_raw / 2) / span_raw;
	} else {
		q = (num - span_raw / 2) / span_raw;
	}
	q += e->ref_lo;
	if (q > INT32_MAX || q < INT32_MIN) {
		return -ERANGE;
	}
	*out = (int32_t)q;
	return 0;
}
