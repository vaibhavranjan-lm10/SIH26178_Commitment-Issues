/*
 * Mode R (blueprint §4.4): the pod reports to the head node on a
 * scheduled slot, it is not polled.  The head node's beacon marks the
 * start of each superframe; pods transmit at
 *   superframe_start + beacon_ms + slot * slot_ms
 * A pod that has lost the beacon keeps its free-running schedule
 * (holdover) — connectivity changes the rate of learning, never whether
 * the pod reports.
 *
 * All times are the pod's local uptime in ms, wrap-safe.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef PRAHARI_LORA_SLOT_H_
#define PRAHARI_LORA_SLOT_H_

#include <stdbool.h>
#include <stdint.h>

struct slot_sched {
	uint32_t superframe_ms;
	uint16_t slot_ms;
	uint16_t beacon_ms;
	uint16_t guard_ms;    /* do not schedule a TX closer than this to 'now' */
	uint32_t holdover_ms; /* beyond this since the last beacon: unsynced */
	uint8_t n_slots;
	uint8_t slot;
	/* state */
	uint32_t epoch_ms;    /* local time of the most recent superframe start */
	uint32_t last_sync_ms;
	bool have_epoch;
	bool synced;
};

/* -EINVAL if the geometry is impossible (slot >= n_slots, slots do not fit). */
int slot_sched_init(struct slot_sched *s, uint32_t superframe_ms, uint16_t slot_ms,
		    uint16_t beacon_ms, uint8_t n_slots, uint8_t slot, uint16_t guard_ms,
		    uint32_t holdover_ms);

/* Beacon received at local time rx_ms; it was sent airtime_ms after the
 * superframe started. */
void slot_sched_sync(struct slot_sched *s, uint32_t rx_ms, uint16_t airtime_ms);

/* Free-running start when no beacon has ever been heard. */
void slot_sched_start_free_running(struct slot_sched *s, uint32_t now_ms);

/* Next TX instant at or after now + guard.  -EAGAIN if no epoch yet. */
int slot_sched_next_tx(const struct slot_sched *s, uint32_t now_ms, uint32_t *tx_ms);

/* Next superframe start at or after now + guard (when to open the beacon
 * receive window).  -EAGAIN if no epoch yet. */
int slot_sched_next_beacon(const struct slot_sched *s, uint32_t now_ms, uint32_t *rx_ms);

/* Refresh the synced flag against holdover; returns it. */
bool slot_sched_update(struct slot_sched *s, uint32_t now_ms);

/* Time-on-air estimate for one LoRa packet (Semtech formula), ms. */
uint32_t lora_time_on_air_ms(uint8_t payload_len, uint8_t sf, uint32_t bw_hz, uint8_t cr_denom,
			     uint16_t preamble, bool explicit_header, bool crc, bool ldro);

#endif /* PRAHARI_LORA_SLOT_H_ */
