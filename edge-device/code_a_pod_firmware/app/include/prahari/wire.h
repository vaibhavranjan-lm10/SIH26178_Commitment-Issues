/*
 * Pod <-> head-node wire format, shared by Mode W (RS-485) and Mode R
 * (LoRa).  Little-endian, CRC-16/CCITT-FALSE over everything before the
 * CRC.  Pure encode/decode; the decoder is what Code B's side (and the
 * tests) use to read a pod report.
 *
 * Note: this is L1 -> L3 pod telemetry, not the L4 mesh.  §7.5's
 * "only 16-byte embeddings" rule applies between head nodes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef PRAHARI_WIRE_H_
#define PRAHARI_WIRE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <prahari/pipeline.h>

#define WIRE_VERSION 1
#define WIRE_REPORT_MAX 200 /* fits one LoRa packet with headroom, one RS-485 frame */

/* Report header flags */
#define WIRE_RF_WAKE       (1u << 0)
#define WIRE_RF_RAIL_VALID (1u << 1)

/* Derived-parameter IDs on the wire are the taxonomy S-numbers. */
#define WIRE_S13 13
#define WIRE_S14 14
#define WIRE_S15 15
#define WIRE_S16 16
#define WIRE_S21 21

uint16_t wire_crc16(const uint8_t *data, size_t len);
void wire_put16(uint8_t *p, uint16_t v);
void wire_put32(uint8_t *p, uint32_t v);
uint16_t wire_get16(const uint8_t *p);
uint32_t wire_get32(const uint8_t *p);

/* Encode a processed report.  Returns byte count or -ENOSPC. */
int wire_encode_report(const struct pod_report *r, uint8_t position, uint8_t *out,
		       size_t len);

/* Decoded view (for tests / the head node). */
struct wire_channel {
	uint8_t id;    /* P-number, or S-number for derived */
	uint8_t flags;
	int32_t value;
};

struct wire_report {
	uint8_t version;
	uint8_t position;
	uint8_t flags;
	uint16_t cycle;
	uint16_t rail_mv;
	uint16_t xdcr_fault;
	uint16_t xdcr_nopower;
	uint16_t xdcr_range;
	uint8_t n_primary;
	uint8_t n_derived;
	struct wire_channel primary[PRAHARI_PARAM_INSITU_COUNT];
	struct wire_channel derived[DER_COUNT];
};

/* 0 ok, -EBADMSG malformed, -EINVAL bad version. */
int wire_decode_report(const uint8_t *buf, size_t len, struct wire_report *out);

/* Head-node beacon (Mode R): marks a superframe start. */
struct wire_beacon {
	uint16_t superframe_idx;
	uint16_t slot_ms;
	uint8_t n_slots;
	uint16_t beacon_ms; /* airtime reserved for the beacon at superframe start */
};
#define WIRE_BEACON_LEN 11 /* 'P' 'B' idx16 slot16 n8 beacon16 crc16 */
int wire_encode_beacon(const struct wire_beacon *b, uint8_t *out, size_t len);
int wire_decode_beacon(const uint8_t *buf, size_t len, struct wire_beacon *out);

/* Mode R uplink packet: 'P' 'R' addr, report payload, crc16. */
int wire_encode_lora_uplink(uint8_t addr, const uint8_t *report, size_t rlen, uint8_t *out,
			    size_t len);
int wire_decode_lora_uplink(const uint8_t *buf, size_t len, uint8_t *addr,
			    const uint8_t **report, size_t *rlen);

#endif /* PRAHARI_WIRE_H_ */
