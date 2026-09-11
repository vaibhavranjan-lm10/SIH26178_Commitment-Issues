/*
 * Transducer descriptor — the unit of configuration in the pod firmware.
 *
 * Blueprint §3.1 interface classes A-D.  A descriptor says: which
 * parameter IDs a transducer reports, at which pod positions it is
 * populated, which switched rail feeds it, and which class driver reads it.
 * Descriptors are generated from devicetree (src/pod_dt_tables.c); the
 * selection logic here is pure and unit-tested.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef PRAHARI_XDCR_H_
#define PRAHARI_XDCR_H_

#include <stddef.h>
#include <stdint.h>
#include <prahari/pod_position.h>

/* Order matches the 'class' enum of prahari,sensor-transducer.yaml (i2c, uart). */
enum xdcr_class {
	XDCR_CLASS_A_I2C = 0,
	XDCR_CLASS_B_UART = 1,
	XDCR_CLASS_C_ADC = 2,
	XDCR_CLASS_D_PULSE = 3,
};

/* Sample flags.  Absence of XDCR_F_VALID *is* the mask (never impute). */
#define XDCR_F_VALID   (1u << 0) /* value present */
#define XDCR_F_RAW     (1u << 1) /* uncalibrated: mV, counts, driver units */
#define XDCR_F_FAULT   (1u << 2) /* read failed / transducer fault */
#define XDCR_F_STALE   (1u << 3) /* held over from an earlier duty window */
#define XDCR_F_NOPOWER (1u << 4) /* rail unavailable (unarmed, budget, window) */
#define XDCR_F_RANGE   (1u << 5) /* out of spec: flagged, value retained (§4.3) */
#define XDCR_F_UNCAL   (1u << 6) /* raw units with no calibration entry: unusable downstream */
#define XDCR_F_ANOMALY (1u << 7) /* z-score gate tripped this cycle */

struct xdcr_sample {
	int32_t value;
	uint8_t param; /* PRAHARI P-ID */
	uint8_t flags;
};

#define XDCR_DOMAIN_NONE (-1) /* passive transducer, no switched rail */

struct xdcr_desc;

struct xdcr_ops {
	int (*init)(const struct xdcr_desc *d);
	/* Fill up to 'max' samples; return count or -errno. */
	int (*read)(const struct xdcr_desc *d, struct xdcr_sample *out, size_t max);
};

struct xdcr_desc {
	const char *name;
	enum xdcr_class cls;
	uint8_t position_mask; /* OR of POD_POS_BIT() */
	int8_t domain;         /* power-domain index or XDCR_DOMAIN_NONE */
	uint8_t n_params;
	const uint8_t *params; /* PRAHARI P-IDs, channel order */
	const struct xdcr_ops *ops;
	void *ctx;
};

/*
 * Pick the transducers populated at 'pos'.  Writes pointers into 'out'
 * (at most 'max'), returns the count actually populated at that position
 * (which may exceed 'max' — caller should size 'out' from xdcr count).
 */
size_t xdcr_select(const struct xdcr_desc *all, size_t n, enum pod_position pos,
		   const struct xdcr_desc **out, size_t max);

const char *xdcr_class_name(enum xdcr_class cls);

#endif /* PRAHARI_XDCR_H_ */
