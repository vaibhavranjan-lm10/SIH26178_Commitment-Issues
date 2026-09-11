/* SPDX-License-Identifier: Apache-2.0 */
#include <prahari/median5.h>

void median5_init(struct median5 *m)
{
	m->n = 0;
	m->head = 0;
	m->last = 0;
}

int32_t median5_push(struct median5 *m, int32_t x)
{
	int32_t s[MEDIAN5_N];

	m->buf[m->head] = x;
	m->head = (uint8_t)((m->head + 1) % MEDIAN5_N);
	if (m->n < MEDIAN5_N) {
		m->n++;
	}

	/* insertion sort of a copy; n <= 5 */
	for (uint8_t i = 0; i < m->n; i++) {
		int32_t v = m->buf[i];
		int8_t j = (int8_t)i - 1;

		while (j >= 0 && s[j] > v) {
			s[j + 1] = s[j];
			j--;
		}
		s[j + 1] = v;
	}
	m->last = s[(m->n - 1) / 2]; /* median; lower middle while filling */
	return m->last;
}

int32_t median5_last(const struct median5 *m)
{
	return m->last;
}

uint8_t median5_count(const struct median5 *m)
{
	return m->n;
}
