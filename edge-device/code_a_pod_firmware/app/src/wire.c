/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <string.h>
#include <prahari/wire.h>

uint16_t wire_crc16(const uint8_t *data, size_t len)
{
	uint16_t crc = 0xFFFF; /* CRC-16/CCITT-FALSE */

	for (size_t i = 0; i < len; i++) {
		crc ^= (uint16_t)data[i] << 8;
		for (int b = 0; b < 8; b++) {
			crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
		}
	}
	return crc;
}

void wire_put16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

void wire_put32(uint8_t *p, uint32_t v)
{
	wire_put16(p, (uint16_t)v);
	wire_put16(p + 2, (uint16_t)(v >> 16));
}

uint16_t wire_get16(const uint8_t *p)
{
	return (uint16_t)(p[0] | (p[1] << 8));
}

uint32_t wire_get32(const uint8_t *p)
{
	return (uint32_t)wire_get16(p) | ((uint32_t)wire_get16(p + 2) << 16);
}

static const uint8_t derived_id[DER_COUNT] = {WIRE_S13, WIRE_S14, WIRE_S15, WIRE_S16, WIRE_S21};

int wire_encode_report(const struct pod_report *r, uint8_t position, uint8_t *out, size_t len)
{
	size_t n = 0;
	uint8_t *count;

	if (len < 16) {
		return -ENOSPC;
	}
	out[n++] = WIRE_VERSION;
	out[n++] = position;
	out[n++] = (uint8_t)((r->wake ? WIRE_RF_WAKE : 0) |
			     (r->self.rail_valid ? WIRE_RF_RAIL_VALID : 0));
	wire_put16(out + n, (uint16_t)r->primary.cycle);
	n += 2;
	wire_put16(out + n, (uint16_t)(r->self.rail_mv < 0 ? 0 : r->self.rail_mv));
	n += 2;
	wire_put16(out + n, r->self.xdcr_fault);
	n += 2;
	wire_put16(out + n, r->self.xdcr_nopower);
	n += 2;
	wire_put16(out + n, r->self.xdcr_range);
	n += 2;

	count = &out[n++];
	*count = 0;
	for (uint8_t p = 1; p <= PRAHARI_PARAM_INSITU_COUNT; p++) {
		if (r->primary.flags[p] == 0) {
			continue; /* never populated: not on the wire at all */
		}
		if (n + 6 > len) {
			return -ENOSPC;
		}
		out[n++] = p;
		out[n++] = r->primary.flags[p];
		wire_put32(out + n, (uint32_t)r->primary.value[p]);
		n += 4;
		(*count)++;
	}

	if (n + 1 > len) {
		return -ENOSPC;
	}
	count = &out[n++];
	*count = 0;
	for (int d = 0; d < DER_COUNT; d++) {
		if (!(r->derived_flags[d] & XDCR_F_VALID)) {
			continue;
		}
		if (n + 6 > len) {
			return -ENOSPC;
		}
		out[n++] = derived_id[d];
		out[n++] = r->derived_flags[d];
		wire_put32(out + n, (uint32_t)r->derived[d]);
		n += 4;
		(*count)++;
	}
	return (int)n;
}

int wire_decode_report(const uint8_t *buf, size_t len, struct wire_report *out)
{
	size_t n = 0;

	if (!buf || !out || len < 15) {
		return -EBADMSG;
	}
	memset(out, 0, sizeof(*out));
	out->version = buf[n++];
	if (out->version != WIRE_VERSION) {
		return -EINVAL;
	}
	out->position = buf[n++];
	out->flags = buf[n++];
	out->cycle = wire_get16(buf + n);
	n += 2;
	out->rail_mv = wire_get16(buf + n);
	n += 2;
	out->xdcr_fault = wire_get16(buf + n);
	n += 2;
	out->xdcr_nopower = wire_get16(buf + n);
	n += 2;
	out->xdcr_range = wire_get16(buf + n);
	n += 2;

	out->n_primary = buf[n++];
	if (out->n_primary > PRAHARI_PARAM_INSITU_COUNT || n + (size_t)out->n_primary * 6 + 1 > len) {
		return -EBADMSG;
	}
	for (uint8_t i = 0; i < out->n_primary; i++) {
		out->primary[i].id = buf[n++];
		out->primary[i].flags = buf[n++];
		out->primary[i].value = (int32_t)wire_get32(buf + n);
		n += 4;
		if (!prahari_param_is_insitu(out->primary[i].id)) {
			return -EBADMSG;
		}
	}
	out->n_derived = buf[n++];
	if (out->n_derived > DER_COUNT || n + (size_t)out->n_derived * 6 > len) {
		return -EBADMSG;
	}
	for (uint8_t i = 0; i < out->n_derived; i++) {
		out->derived[i].id = buf[n++];
		out->derived[i].flags = buf[n++];
		out->derived[i].value = (int32_t)wire_get32(buf + n);
		n += 4;
	}
	return (n == len) ? 0 : -EBADMSG;
}

int wire_encode_beacon(const struct wire_beacon *b, uint8_t *out, size_t len)
{
	if (len < WIRE_BEACON_LEN) {
		return -ENOSPC;
	}
	out[0] = 'P';
	out[1] = 'B';
	wire_put16(out + 2, b->superframe_idx);
	wire_put16(out + 4, b->slot_ms);
	out[6] = b->n_slots;
	wire_put16(out + 7, b->beacon_ms);
	wire_put16(out + 9, wire_crc16(out, 9));
	return WIRE_BEACON_LEN;
}

int wire_decode_beacon(const uint8_t *buf, size_t len, struct wire_beacon *out)
{
	if (len != WIRE_BEACON_LEN || buf[0] != 'P' || buf[1] != 'B' ||
	    wire_get16(buf + 9) != wire_crc16(buf, 9)) {
		return -EBADMSG;
	}
	out->superframe_idx = wire_get16(buf + 2);
	out->slot_ms = wire_get16(buf + 4);
	out->n_slots = buf[6];
	out->beacon_ms = wire_get16(buf + 7);
	return 0;
}

int wire_encode_lora_uplink(uint8_t addr, const uint8_t *report, size_t rlen, uint8_t *out,
			    size_t len)
{
	if (rlen > WIRE_REPORT_MAX || len < rlen + 5) {
		return -ENOSPC;
	}
	out[0] = 'P';
	out[1] = 'R';
	out[2] = addr;
	memcpy(out + 3, report, rlen);
	wire_put16(out + 3 + rlen, wire_crc16(out, 3 + rlen));
	return (int)(rlen + 5);
}

int wire_decode_lora_uplink(const uint8_t *buf, size_t len, uint8_t *addr,
			    const uint8_t **report, size_t *rlen)
{
	if (len < 5 || buf[0] != 'P' || buf[1] != 'R' ||
	    wire_get16(buf + len - 2) != wire_crc16(buf, len - 2)) {
		return -EBADMSG;
	}
	*addr = buf[2];
	*report = buf + 3;
	*rlen = len - 5;
	return 0;
}
