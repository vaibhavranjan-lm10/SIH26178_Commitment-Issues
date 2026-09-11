/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <string.h>
#include <hn/alert.h>

void alert_init(struct alert_out *a, uint16_t adv_on_ms, uint16_t adv_off_ms,
		uint32_t max_duration_ms)
{
	memset(a, 0, sizeof(*a));
	a->adv_on_ms = adv_on_ms ? adv_on_ms : 1000;
	a->adv_off_ms = adv_off_ms ? adv_off_ms : 2000;
	a->max_duration_ms = max_duration_ms;
}

int alert_command(struct alert_out *a, enum alert_level level, uint32_t duration_ms,
		  uint32_t now_ms)
{
	if (level > ALERT_WARNING) {
		return -EINVAL;
	}
	a->commands++;
	if (level == ALERT_OFF || duration_ms == 0) {
		a->level = ALERT_OFF;
		a->siren = false;
		return 0;
	}
	if (a->max_duration_ms && duration_ms > a->max_duration_ms) {
		duration_ms = a->max_duration_ms;
	}
	a->level = level;
	a->t_start = now_ms;
	a->t_end = now_ms + duration_ms;
	return 0;
}

bool alert_tick(struct alert_out *a, uint32_t now_ms)
{
	if (a->level == ALERT_OFF) {
		a->siren = false;
		return false;
	}
	if ((int32_t)(now_ms - a->t_end) >= 0) {
		a->level = ALERT_OFF;
		a->siren = false;
		a->expiries++;
		return false;
	}
	if (a->level == ALERT_WARNING) {
		a->siren = true;
	} else {
		uint32_t period = (uint32_t)a->adv_on_ms + a->adv_off_ms;
		uint32_t ph = (now_ms - a->t_start) % period;

		a->siren = ph < a->adv_on_ms;
	}
	return a->siren;
}

uint32_t alert_remaining_ms(const struct alert_out *a, uint32_t now_ms)
{
	if (a->level == ALERT_OFF || (int32_t)(now_ms - a->t_end) >= 0) {
		return 0;
	}
	return a->t_end - now_ms;
}
