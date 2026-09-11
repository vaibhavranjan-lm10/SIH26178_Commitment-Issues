/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <string.h>
#include <prahari/rs485.h>
#include <prahari/wire.h>

bool rs485_addr_valid(uint8_t addr)
{
	return addr >= RS485_ADDR_MIN && addr <= RS485_ADDR_MAX;
}

int rs485_frame_encode(const struct rs485_frame *f, uint8_t *out, size_t len)
{
	size_t n = 4 + f->len + 2;

	if (f->len > RS485_PAYLOAD_MAX) {
		return -EINVAL;
	}
	if (len < n) {
		return -ENOSPC;
	}
	out[0] = RS485_SOF;
	out[1] = f->addr;
	out[2] = f->cmd;
	out[3] = f->len;
	memcpy(out + 4, f->payload, f->len);
	wire_put16(out + 4 + f->len, wire_crc16(out + 1, 3 + f->len));
	return (int)n;
}

void rs485_parser_init(struct rs485_parser *p)
{
	memset(p, 0, sizeof(*p));
	p->state = RS485_RX_SOF;
}

static void reset(struct rs485_parser *p)
{
	p->state = RS485_RX_SOF;
	p->got = 0;
}

bool rs485_parser_byte(struct rs485_parser *p, uint8_t b, uint32_t now_ms)
{
	p->last_byte_ms = now_ms;
	switch (p->state) {
	case RS485_RX_SOF:
		if (b == RS485_SOF) {
			p->state = RS485_RX_ADDR;
		}
		return false;
	case RS485_RX_ADDR:
		p->frame.addr = b;
		p->state = RS485_RX_CMD;
		return false;
	case RS485_RX_CMD:
		p->frame.cmd = b;
		p->state = RS485_RX_LEN;
		return false;
	case RS485_RX_LEN:
		if (b > RS485_PAYLOAD_MAX) {
			reset(p);
			return false;
		}
		p->frame.len = b;
		p->got = 0;
		p->state = b ? RS485_RX_PAYLOAD : RS485_RX_CRC_LO;
		return false;
	case RS485_RX_PAYLOAD:
		p->frame.payload[p->got++] = b;
		if (p->got == p->frame.len) {
			p->state = RS485_RX_CRC_LO;
		}
		return false;
	case RS485_RX_CRC_LO:
		p->crc_rx = b;
		p->state = RS485_RX_CRC_HI;
		return false;
	case RS485_RX_CRC_HI: {
		uint8_t hdr[3] = {p->frame.addr, p->frame.cmd, p->frame.len};
		uint16_t crc = 0xFFFF;
		uint8_t tmp[3 + RS485_PAYLOAD_MAX];

		p->crc_rx |= (uint16_t)b << 8;
		memcpy(tmp, hdr, 3);
		memcpy(tmp + 3, p->frame.payload, p->frame.len);
		crc = wire_crc16(tmp, 3 + p->frame.len);
		reset(p);
		if (crc == p->crc_rx) {
			p->frames++;
			return true;
		}
		p->crc_errors++;
		return false;
	}
	default:
		reset(p);
		return false;
	}
}

void rs485_parser_idle(struct rs485_parser *p, uint32_t now_ms, uint32_t gap_ms)
{
	if (p->state != RS485_RX_SOF && (uint32_t)(now_ms - p->last_byte_ms) > gap_ms) {
		reset(p);
	}
}

int rs485_slave_handle(const struct rs485_slave *me, const struct rs485_frame *in,
		       struct rs485_frame *out)
{
	bool broadcast = in->addr == RS485_ADDR_BROADCAST;

	if (!me || !rs485_addr_valid(me->addr)) {
		return -EINVAL;
	}
	if (in->addr != me->addr && !broadcast) {
		return 0; /* someone else's poll: stay silent */
	}
	if (in->cmd & 0x80) {
		return 0; /* another pod's reply on the bus, never answer a reply */
	}

	memset(out, 0, sizeof(*out));
	out->addr = me->addr; /* replies carry the pod's own address */

	switch (in->cmd) {
	case RS485_CMD_POLL:
		if (broadcast) {
			return 0; /* a broadcast poll would collide: §5.2 polls by address */
		}
		out->cmd = RS485_CMD_REPORT;
		out->len = me->report ? me->report_len : 0;
		if (out->len) {
			memcpy(out->payload, me->report, out->len);
		}
		return 1;
	case RS485_CMD_ARM:
		if (in->len >= 1 && me->arm_cb) {
			me->arm_cb(me->ctx, in->payload[0] != 0);
		}
		if (broadcast) {
			return 0;
		}
		out->cmd = RS485_CMD_ACK;
		out->len = 1;
		out->payload[0] = RS485_CMD_ARM;
		return 1;
	case RS485_CMD_PING:
		if (broadcast) {
			return 0;
		}
		out->cmd = RS485_CMD_PONG;
		out->len = 3;
		out->payload[0] = me->position;
		out->payload[1] = me->fw_major;
		out->payload[2] = me->fw_minor;
		return 1;
	default:
		if (broadcast) {
			return 0;
		}
		out->cmd = RS485_CMD_NAK;
		out->len = 1;
		out->payload[0] = in->cmd;
		return 1;
	}
}
