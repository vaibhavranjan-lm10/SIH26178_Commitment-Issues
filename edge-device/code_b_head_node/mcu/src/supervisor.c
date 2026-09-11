/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <string.h>
#include <hn/supervisor.h>

void sup_init(struct supervisor *s, uint32_t now_ms)
{
	memset(s, 0, sizeof(*s));
	s->t_init = now_ms;
}

int sup_register(struct supervisor *s, const char *name, uint32_t deadline_ms, bool required_at_boot)
{
	if (s->n >= SUP_MAX_CHANNELS || deadline_ms == 0) {
		return -ENOSPC;
	}
	s->ch[s->n].name = name;
	s->ch[s->n].deadline_ms = deadline_ms;
	s->ch[s->n].last_ms = s->t_init;
	s->ch[s->n].armed = required_at_boot;
	s->ch[s->n].required_at_boot = required_at_boot;
	return s->n++;
}

void sup_checkin(struct supervisor *s, int ch, uint32_t now_ms)
{
	if (ch < 0 || ch >= s->n) {
		return;
	}
	s->ch[ch].last_ms = now_ms;
	s->ch[ch].armed = true;
}

uint32_t sup_stale_mask(const struct supervisor *s, uint32_t now_ms)
{
	uint32_t m = 0;

	for (uint8_t i = 0; i < s->n; i++) {
		if (s->ch[i].armed && (uint32_t)(now_ms - s->ch[i].last_ms) > s->ch[i].deadline_ms) {
			m |= 1u << i;
		}
	}
	return m;
}

bool sup_should_feed(struct supervisor *s, uint32_t now_ms)
{
	if (sup_stale_mask(s, now_ms)) {
		s->refusals++;
		return false;
	}
	s->feeds++;
	return true;
}
