/*
 * Two-point calibration (blueprint §4.3): per-channel coefficients stored
 * in EEPROM as the two reference pairs (raw, reference) so they can be
 * re-derived and audited.  y = ref_lo + (x - raw_lo) * (ref_hi - ref_lo)
 * / (raw_hi - raw_lo), exact integer arithmetic, milli-units throughout.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef PRAHARI_CALIB_H_
#define PRAHARI_CALIB_H_

#include <stddef.h>
#include <stdint.h>

#define CALIB_MAX_ENTRIES 16
#define CALIB_MAGIC0 'C'
#define CALIB_MAGIC1 'L'
#define CALIB_VERSION 1
#define CALIB_HDR_LEN 4   /* magic0, magic1, version, count */
#define CALIB_ENTRY_LEN 17 /* param + 4 x int32 LE */
#define CALIB_BLOB_MAX (CALIB_HDR_LEN + CALIB_MAX_ENTRIES * CALIB_ENTRY_LEN + 1)

struct calib_entry {
	uint8_t param; /* PRAHARI P-ID */
	int32_t raw_lo;
	int32_t ref_lo;
	int32_t raw_hi;
	int32_t ref_hi;
};

struct calib_table {
	struct calib_entry e[CALIB_MAX_ENTRIES];
	uint8_t n;
};

/* Serialise for EEPROM.  Returns byte count or -EINVAL/-ENOSPC. */
int calib_table_encode(const struct calib_table *t, uint8_t *out, size_t len);
/* Parse an EEPROM image.  0 ok; -ENOENT blank; -EINVAL malformed/CRC. */
int calib_table_decode(const uint8_t *blob, size_t len, struct calib_table *t);

const struct calib_entry *calib_find(const struct calib_table *t, uint8_t param);
/* Apply two-point mapping.  -EINVAL if the entry is degenerate (raw_hi == raw_lo). */
int calib_apply(const struct calib_entry *e, int32_t raw, int32_t *out);

#endif /* PRAHARI_CALIB_H_ */
