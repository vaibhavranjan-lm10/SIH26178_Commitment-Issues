/*
 * Z-score anomaly gate against a short rolling mean (blueprint §4.3).
 * The window is the ONLY history this firmware keeps: a few tens of
 * samples, minutes at the pod cadence.  Multi-day state belongs to Code B.
 * Integer arithmetic; z is reported in hundredths.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef PRAHARI_ZGATE_H_
#define PRAHARI_ZGATE_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef CONFIG_PRAHARI_ZGATE_WINDOW
#define ZGATE_WINDOW CONFIG_PRAHARI_ZGATE_WINDOW
#else
#define ZGATE_WINDOW 12
#endif

struct zgate {
	int32_t win[ZGATE_WINDOW];
	uint8_t n;
	uint8_t head;
};

void zgate_init(struct zgate *g);

/*
 * Score x against the window (which excludes x), then push x.
 *  thr_centi:     |z| * 100 at or above which x is anomalous
 *  floor_permille: std floor as a fraction of |mean| (avoids div-by-zero
 *                  on a flat signal); absolute floor of 1 milli-unit
 * Returns 1 anomalous, 0 normal, -EAGAIN while the window is still filling.
 */
int zgate_update(struct zgate *g, int32_t x, uint16_t thr_centi, uint16_t floor_permille,
		 int32_t *z_centi);

bool zgate_full(const struct zgate *g);

#endif /* PRAHARI_ZGATE_H_ */
