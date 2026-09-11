/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <string.h>
#include <prahari/lora_slot.h>

int slot_sched_init(struct slot_sched *s, uint32_t superframe_ms, uint16_t slot_ms,
		    uint16_t beacon_ms, uint8_t n_slots, uint8_t slot, uint16_t guard_ms,
		    uint32_t holdover_ms)
{
	if (!s || n_slots == 0 || slot >= n_slots || slot_ms == 0 || superframe_ms == 0 ||
	    (uint32_t)beacon_ms + (uint32_t)n_slots * slot_ms > superframe_ms ||
	    superframe_ms > 0x3FFFFFFFu) {
		return -EINVAL;
	}
	memset(s, 0, sizeof(*s));
	s->superframe_ms = superframe_ms;
	s->slot_ms = slot_ms;
	s->beacon_ms = beacon_ms;
	s->n_slots = n_slots;
	s->slot = slot;
	s->guard_ms = guard_ms;
	s->holdover_ms = holdover_ms;
	return 0;
}

void slot_sched_sync(struct slot_sched *s, uint32_t rx_ms, uint16_t airtime_ms)
{
	s->epoch_ms = rx_ms - airtime_ms;
	s->last_sync_ms = rx_ms;
	s->have_epoch = true;
	s->synced = true;
}

void slot_sched_start_free_running(struct slot_sched *s, uint32_t now_ms)
{
	if (!s->have_epoch) {
		s->epoch_ms = now_ms;
		s->last_sync_ms = now_ms;
		s->have_epoch = true;
		s->synced = false;
	}
}

/* Smallest t = epoch + offset + k*superframe with t >= now + guard (wrap-safe). */
static uint32_t next_after(const struct slot_sched *s, uint32_t now_ms, uint32_t offset)
{
	uint32_t base = s->epoch_ms + offset;
	uint32_t target = now_ms + s->guard_ms;
	int32_t d = (int32_t)(target - base);
	uint32_t k;

	if (d <= 0) {
		return base;
	}
	k = ((uint32_t)d + s->superframe_ms - 1) / s->superframe_ms;
	return base + k * s->superframe_ms;
}

int slot_sched_next_tx(const struct slot_sched *s, uint32_t now_ms, uint32_t *tx_ms)
{
	if (!s->have_epoch) {
		return -EAGAIN;
	}
	*tx_ms = next_after(s, now_ms, (uint32_t)s->beacon_ms + (uint32_t)s->slot * s->slot_ms);
	return 0;
}

int slot_sched_next_beacon(const struct slot_sched *s, uint32_t now_ms, uint32_t *rx_ms)
{
	if (!s->have_epoch) {
		return -EAGAIN;
	}
	*rx_ms = next_after(s, now_ms, 0);
	return 0;
}

bool slot_sched_update(struct slot_sched *s, uint32_t now_ms)
{
	if (s->synced && (uint32_t)(now_ms - s->last_sync_ms) > s->holdover_ms) {
		s->synced = false;
	}
	return s->synced;
}

uint32_t lora_time_on_air_ms(uint8_t payload_len, uint8_t sf, uint32_t bw_hz, uint8_t cr_denom,
			     uint16_t preamble, bool explicit_header, bool crc, bool ldro)
{
	/* Semtech SX126x datasheet symbol-count formula, integer arithmetic in us. */
	uint32_t tsym_us = ((uint32_t)1 << sf) * 1000000u / bw_hz;
	int32_t num = 8 * (int32_t)payload_len - 4 * sf + 28 + (crc ? 16 : 0) -
		      (explicit_header ? 0 : 20);
	int32_t den = 4 * ((int32_t)sf - (ldro ? 2 : 0));
	int32_t nsym;
	uint32_t tpre_us, tpay_us;

	if (den <= 0 || cr_denom < 5 || cr_denom > 8) {
		return 0;
	}
	nsym = (num <= 0) ? 0 : ((num + den - 1) / den) * cr_denom;
	tpre_us = (preamble * 4 + 17) * tsym_us / 4; /* (preamble + 4.25) symbols */
	tpay_us = (uint32_t)(8 + nsym) * tsym_us;
	return (tpre_us + tpay_us + 999) / 1000;
}
