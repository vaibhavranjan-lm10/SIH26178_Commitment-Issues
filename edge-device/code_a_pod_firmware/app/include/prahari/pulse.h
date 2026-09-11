/*
 * Debounced pulse counter for class D transducers (blueprint §3.1).
 * Pure; the GPIO ISR in drivers/xdcr_pulse.c feeds edges in.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef PRAHARI_PULSE_H_
#define PRAHARI_PULSE_H_

#include <stdbool.h>
#include <stdint.h>

struct pulse_counter {
	uint32_t total;      /* accepted edges since init (cumulative) */
	uint32_t taken;      /* total at last take() */
	uint32_t last_ms;    /* time of last accepted edge */
	uint16_t debounce_ms;
	bool primed;
};

void pulse_counter_init(struct pulse_counter *pc, uint16_t debounce_ms);
/* Feed one edge at time t_ms; returns true if it was counted. */
bool pulse_counter_edge(struct pulse_counter *pc, uint32_t t_ms);
uint32_t pulse_counter_total(const struct pulse_counter *pc);
/* Pulses since the previous take (rate-style read). */
uint32_t pulse_counter_take(struct pulse_counter *pc);

#endif /* PRAHARI_PULSE_H_ */
