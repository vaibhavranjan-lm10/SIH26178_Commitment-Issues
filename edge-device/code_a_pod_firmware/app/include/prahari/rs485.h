/*
 * Mode W (blueprint §4.4): RS-485 half-duplex multi-drop, head node is
 * bus master, pods are polled slaves by address (§5.2 sweep).  A pod
 * never transmits unsolicited; a pod that is not polled stays silent
 * and needs no logic to notice.
 *
 * Frame:  SOF 0x7E | addr | cmd | len | payload[len] | crc16 (LE)
 * CRC-16/CCITT-FALSE over addr..payload.  No byte stuffing: frames are
 * length-delimited and the parser resyncs on CRC failure or idle gap.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef PRAHARI_RS485_H_
#define PRAHARI_RS485_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RS485_SOF 0x7E
#define RS485_ADDR_MASTER 0x00
#define RS485_ADDR_BROADCAST 0xFF
#define RS485_ADDR_MIN 1
#define RS485_ADDR_MAX 247
#define RS485_PAYLOAD_MAX 200
#define RS485_FRAME_MAX (4 + RS485_PAYLOAD_MAX + 2)

/* Master -> pod */
#define RS485_CMD_POLL 0x01 /* reply: REPORT */
#define RS485_CMD_ARM  0x02 /* payload[0] = 1 arm / 0 disarm; reply ACK if unicast */
#define RS485_CMD_PING 0x03 /* reply: PONG {position, fw major, fw minor} */
/* Pod -> master */
#define RS485_CMD_REPORT 0x81
#define RS485_CMD_ACK    0x82
#define RS485_CMD_PONG   0x83
#define RS485_CMD_NAK    0xFE /* payload[0] = offending cmd */

struct rs485_frame {
	uint8_t addr;
	uint8_t cmd;
	uint8_t len;
	uint8_t payload[RS485_PAYLOAD_MAX];
};

int rs485_frame_encode(const struct rs485_frame *f, uint8_t *out, size_t len);

/* Byte-wise receive parser. */
enum rs485_rx_state {
	RS485_RX_SOF,
	RS485_RX_ADDR,
	RS485_RX_CMD,
	RS485_RX_LEN,
	RS485_RX_PAYLOAD,
	RS485_RX_CRC_LO,
	RS485_RX_CRC_HI,
};

struct rs485_parser {
	enum rs485_rx_state state;
	struct rs485_frame frame;
	uint8_t got;
	uint16_t crc_rx;
	uint32_t last_byte_ms;
	uint32_t crc_errors;
	uint32_t frames;
};

void rs485_parser_init(struct rs485_parser *p);
/* Feed one byte; returns true when a CRC-valid frame is complete in p->frame. */
bool rs485_parser_byte(struct rs485_parser *p, uint8_t b, uint32_t now_ms);
/* Drop a partial frame if the bus has been idle longer than gap_ms. */
void rs485_parser_idle(struct rs485_parser *p, uint32_t now_ms, uint32_t gap_ms);

/* Slave behaviour. */
struct rs485_slave {
	uint8_t addr;
	uint8_t position;
	uint8_t fw_major;
	uint8_t fw_minor;
	/* latest report payload to hand out on POLL (NULL/0 = nothing yet) */
	const uint8_t *report;
	uint8_t report_len;
	/* head-node command hooks */
	void (*arm_cb)(void *ctx, bool armed);
	void *ctx;
};

/*
 * Handle a received frame.  Returns 1 if 'out' must be transmitted,
 * 0 if the pod stays silent (not ours, or a broadcast), -EINVAL on a
 * bad slave config.
 */
int rs485_slave_handle(const struct rs485_slave *me, const struct rs485_frame *in,
		       struct rs485_frame *out);

bool rs485_addr_valid(uint8_t addr);

#endif /* PRAHARI_RS485_H_ */
