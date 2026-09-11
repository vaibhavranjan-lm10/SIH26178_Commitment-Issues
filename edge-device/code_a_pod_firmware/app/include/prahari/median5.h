/*
 * Median-of-5 spike rejection (blueprint §4.3).  While the window is
 * filling, the median of what is held is returned (lower middle for an
 * even count).  A single outlier never becomes the output; a deviation
 * sustained for 3 of 5 samples does — that is the point where the
 * anomaly gate, not this filter, has to react.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef PRAHARI_MEDIAN5_H_
#define PRAHARI_MEDIAN5_H_

#include <stdint.h>

#define MEDIAN5_N 5

struct median5 {
	int32_t buf[MEDIAN5_N];
	int32_t last;
	uint8_t n;
	uint8_t head;
};

void median5_init(struct median5 *m);
int32_t median5_push(struct median5 *m, int32_t x); /* returns the new median */
int32_t median5_last(const struct median5 *m);
uint8_t median5_count(const struct median5 *m);

#endif /* PRAHARI_MEDIAN5_H_ */
