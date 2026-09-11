/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <prahari/zgate.h>

void zgate_init(struct zgate *g)
{
	g->n = 0;
	g->head = 0;
}

bool zgate_full(const struct zgate *g)
{
	return g->n == ZGATE_WINDOW;
}

static uint32_t isqrt64(uint64_t v)
{
	uint64_t r = 0, bit = (uint64_t)1 << 62;

	while (bit > v) {
		bit >>= 2;
	}
	while (bit) {
		if (v >= r + bit) {
			v -= r + bit;
			r = (r >> 1) + bit;
		} else {
			r >>= 1;
		}
		bit >>= 2;
	}
	return (uint32_t)r;
}

static void push(struct zgate *g, int32_t x)
{
	g->win[g->head] = x;
	g->head = (uint8_t)((g->head + 1) % ZGATE_WINDOW);
	if (g->n < ZGATE_WINDOW) {
		g->n++;
	}
}

int zgate_update(struct zgate *g, int32_t x, uint16_t thr_centi, uint16_t floor_permille,
		 int32_t *z_centi)
{
	int64_t sum = 0, mean, var = 0, dev, floor_v, z;
	uint64_t std;
	int rc;

	if (z_centi) {
		*z_centi = 0;
	}
	if (!zgate_full(g)) {
		push(g, x);
		return -EAGAIN;
	}
	for (uint8_t i = 0; i < ZGATE_WINDOW; i++) {
		sum += g->win[i];
	}
	mean = sum / ZGATE_WINDOW;
	for (uint8_t i = 0; i < ZGATE_WINDOW; i++) {
		int64_t d = (int64_t)g->win[i] - mean;

		var += d * d;
	}
	var /= ZGATE_WINDOW;
	std = isqrt64((uint64_t)var);
	floor_v = (mean < 0 ? -mean : mean) * floor_permille / 1000;
	if (floor_v < 1) {
		floor_v = 1;
	}
	if ((int64_t)std < floor_v) {
		std = (uint64_t)floor_v;
	}
	dev = (int64_t)x - mean;
	z = dev * 100 / (int64_t)std;
	if (z > INT32_MAX) {
		z = INT32_MAX;
	} else if (z < INT32_MIN) {
		z = INT32_MIN;
	}
	if (z_centi) {
		*z_centi = (int32_t)z;
	}
	rc = ((z < 0 ? -z : z) >= thr_centi) ? 1 : 0;
	push(g, x);
	return rc;
}
