/*
 * Local siren / GPIO relay output (blueprint §6.8).  The DECISION is the
 * Linux side's; this is only the actuator: "sound level L for N seconds"
 * arrives over the RPC bridge and is honoured until it expires or is
 * silenced.  Advisory = intermittent pattern, warning = continuous.
 * Pure; time injected.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef HN_ALERT_H_
#define HN_ALERT_H_

#include <stdbool.h>
#include <stdint.h>

enum alert_level {
	ALERT_OFF = 0,
	ALERT_ADVISORY = 1,
	ALERT_WARNING = 2,
};

struct alert_out {
	enum alert_level level;
	uint32_t t_start;
	uint32_t t_end;
	bool siren;              /* current output state */
	uint16_t adv_on_ms, adv_off_ms;
	uint32_t max_duration_ms; /* cap on any single command */
	uint32_t commands, expiries;
};

void alert_init(struct alert_out *a, uint16_t adv_on_ms, uint16_t adv_off_ms,
		uint32_t max_duration_ms);
/* Command from the Linux side.  level OFF silences.  -EINVAL bad level. */
int alert_command(struct alert_out *a, enum alert_level level, uint32_t duration_ms,
		  uint32_t now_ms);
/* Advance; returns desired siren state. */
bool alert_tick(struct alert_out *a, uint32_t now_ms);
uint32_t alert_remaining_ms(const struct alert_out *a, uint32_t now_ms);

#endif /* HN_ALERT_H_ */
