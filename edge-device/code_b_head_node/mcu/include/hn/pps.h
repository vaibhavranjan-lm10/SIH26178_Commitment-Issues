/*
 * GNSS pulse-per-second discipline (blueprint §6.3: the PPS matters more
 * than the fix).  Pairs PPS edge timestamps from the local free-running
 * microsecond clock with the UTC second the receiver reports, estimates
 * the local oscillator's period, rejects glitches, and converts local
 * time to UTC.  Holdover keeps the last good estimate when pulses stop.
 *
 * Pure: local time is a uint32 microsecond counter (wraps every ~71 min),
 * handled with wrap-safe differences.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef HN_PPS_H_
#define HN_PPS_H_

#include <stdbool.h>
#include <stdint.h>

enum pps_state {
	PPS_UNLOCKED = 0, /* no valid pulses yet */
	PPS_LOCKED,       /* pulses arriving, UTC known */
	PPS_HOLDOVER,     /* pulses stopped; extrapolating */
};

struct pps_disc {
	uint32_t nominal_us;     /* 1 000 000 */
	uint32_t tol_us;         /* accept interval within nominal +/- tol */
	uint8_t lock_pulses;     /* consecutive good pulses before LOCKED */
	uint32_t holdover_us;    /* silence after which state -> HOLDOVER */
	/* state */
	enum pps_state state;
	bool have_edge;
	uint32_t last_edge_us;
	uint32_t period_us;      /* estimated local us per true second (x1) */
	uint32_t period_frac;    /* fractional part, 1/65536 us */
	uint8_t good_run;
	uint32_t glitches;
	uint32_t pulses;
	bool utc_valid;
	uint64_t utc_s_at_edge;  /* UTC seconds of last_edge_us */
};

void pps_init(struct pps_disc *d, uint32_t tol_us, uint8_t lock_pulses, uint32_t holdover_us);
/* PPS rising edge at local time t_us.  Returns 1 accepted, 0 rejected (glitch). */
int pps_edge(struct pps_disc *d, uint32_t t_us);
/* GNSS says: the most recent PPS edge marked UTC second utc_s. */
void pps_set_utc(struct pps_disc *d, uint64_t utc_s);
/* Advance time (holdover detection). */
enum pps_state pps_tick(struct pps_disc *d, uint32_t now_us);
/* Convert a local instant to UTC ms.  -EAGAIN until UTC is known. */
int pps_local_to_utc_ms(const struct pps_disc *d, uint32_t t_us, uint64_t *utc_ms);
/* Estimated local clock error, parts per million (signed). */
int32_t pps_drift_ppm(const struct pps_disc *d);

#endif /* HN_PPS_H_ */
