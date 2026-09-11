/* SPDX-License-Identifier: Apache-2.0 */
#include <prahari/pulse.h>

void pulse_counter_init(struct pulse_counter *pc, uint16_t debounce_ms)
{
	pc->total = 0;
	pc->taken = 0;
	pc->last_ms = 0;
	pc->debounce_ms = debounce_ms;
	pc->primed = false;
}

bool pulse_counter_edge(struct pulse_counter *pc, uint32_t t_ms)
{
	if (pc->primed && (uint32_t)(t_ms - pc->last_ms) < pc->debounce_ms) {
		return false;
	}
	pc->primed = true;
	pc->last_ms = t_ms;
	pc->total++;
	return true;
}

uint32_t pulse_counter_total(const struct pulse_counter *pc)
{
	return pc->total;
}

uint32_t pulse_counter_take(struct pulse_counter *pc)
{
	uint32_t delta = pc->total - pc->taken;

	pc->taken = pc->total;
	return delta;
}
