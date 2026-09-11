/*
 * RS-485 bus master (blueprint §5.2): one sweep per cadence tick, pods
 * polled by address in sequence; a silent pod is marked missing and the
 * sweep moves on — it is never stalled.
 *
 * Frame format and CRC come from Code A's protocol definition
 * (code_a_pod_firmware/app/include/prahari/rs485.h, wire.h), compiled
 * into both sides.  Pure sequencer: the UART send and the clock are
 * injected.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef HN_BUS_MASTER_H_
#define HN_BUS_MASTER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <prahari/rs485.h>
#include <prahari/wire.h>

#define BUS_MAX_PODS 16

enum bus_state {
	BUS_IDLE = 0,
	BUS_WAIT_REPLY,
	BUS_TURNAROUND,
};

struct bus_pod {
	uint8_t addr;
	bool present;           /* answered the last poll */
	uint8_t misses;         /* consecutive silent polls (saturating) */
	uint32_t polls, replies, timeouts, bad_replies;
	uint32_t t_last_reply;
	uint8_t report[WIRE_REPORT_MAX];
	uint8_t report_len;
};

enum bus_event {
	BUS_EVT_NONE = 0,
	BUS_EVT_REPORT,   /* pods[idx] delivered a report */
	BUS_EVT_MISSING,  /* pods[idx] did not answer */
	BUS_EVT_SWEEP_DONE,
};

struct bus_master {
	struct bus_pod pods[BUS_MAX_PODS];
	uint8_t n_pods;
	uint16_t reply_timeout_ms;
	uint16_t turnaround_ms;   /* bus idle between a reply and the next poll */
	/* injected */
	int (*send)(void *ctx, const uint8_t *buf, size_t len);
	void *ctx;
	/* state */
	enum bus_state state;
	uint8_t idx;
	uint32_t t_state;
	uint32_t sweeps;
	uint8_t seq;
};

int bus_master_init(struct bus_master *b, const uint8_t *addrs, uint8_t n, uint16_t reply_timeout_ms,
		    uint16_t turnaround_ms, int (*send)(void *, const uint8_t *, size_t), void *ctx);

/* Begin a sweep at 'now'.  -EBUSY if one is in progress, -ENODEV if no pods. */
int bus_sweep_start(struct bus_master *b, uint32_t now_ms);
bool bus_sweep_active(const struct bus_master *b);

/* A CRC-valid frame arrived from the bus. */
enum bus_event bus_rx_frame(struct bus_master *b, const struct rs485_frame *f, uint32_t now_ms,
			    uint8_t *pod_idx);
/* Advance timeouts.  Call often (every few ms). */
enum bus_event bus_tick(struct bus_master *b, uint32_t now_ms, uint8_t *pod_idx);

/* Unsolicited master commands (outside a sweep). */
int bus_send_arm(struct bus_master *b, uint8_t addr, bool armed); /* addr 0xFF = all */

/* Upper bound on one sweep's duration: every pod silent. */
uint32_t bus_sweep_worst_case_ms(const struct bus_master *b);

#endif /* HN_BUS_MASTER_H_ */
