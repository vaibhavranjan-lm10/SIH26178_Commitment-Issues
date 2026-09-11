/*
 * Pod processing pipeline (blueprint §4.3), applied to each frame:
 *   calibrate -> median-of-5 -> range gate (flag) -> z-score gate (wake)
 *   -> instantaneous derivations -> self-report.
 * Pure; the only state is the short median/z windows.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef PRAHARI_PIPELINE_H_
#define PRAHARI_PIPELINE_H_

#include <stdbool.h>
#include <stdint.h>
#include <prahari/calib.h>
#include <prahari/median5.h>
#include <prahari/sampler.h>
#include <prahari/xdcr.h>
#include <prahari/zgate.h>

#ifdef CONFIG_PRAHARI_MAX_CHANNELS
#define PIPELINE_MAX_CHANNELS CONFIG_PRAHARI_MAX_CHANNELS
#else
#define PIPELINE_MAX_CHANNELS 12
#endif

/* Derived (SECONDARY) parameters a pod can compute — IDs from
 * prahari_parameters.md.  Everything else in that tier needs history. */
enum pod_derived {
	DER_S13_VPD = 0,
	DER_S14_DEW_POINT,
	DER_S15_WIND_U,
	DER_S16_WIND_V,
	DER_S21_HEAT_INDEX,
	DER_COUNT,
};

struct pod_range {
	uint8_t param;
	int32_t lo;
	int32_t hi;
};

struct pipeline_cfg {
	const struct calib_table *calib;   /* may be NULL: nothing calibrated */
	const struct pod_range *ranges;    /* may be NULL */
	size_t n_ranges;
	uint16_t z_thr_centi;
	uint16_t z_floor_permille;
};

struct pipeline {
	struct pipeline_cfg cfg;
	uint8_t slot_param[PIPELINE_MAX_CHANNELS];
	uint8_t n_slots;
	struct median5 med[PIPELINE_MAX_CHANNELS];
	struct zgate zg[PIPELINE_MAX_CHANNELS];
};

struct pod_self_report {
	int32_t rail_mv;
	bool rail_valid;
	uint16_t xdcr_fault;    /* bit i: selected transducer i failed init/read */
	uint16_t xdcr_nopower;  /* bit i: rail unavailable this cycle */
	uint16_t xdcr_range;    /* bit i: a channel of i was out of spec */
};

struct pod_report {
	struct pod_frame primary;                   /* processed P-channels */
	int32_t derived[DER_COUNT];
	uint8_t derived_flags[DER_COUNT];
	uint64_t wake_params;                       /* bit p: P-ID p tripped the gate */
	bool wake;
	struct pod_self_report self;
};

int pipeline_init(struct pipeline *p, const struct pipeline_cfg *cfg,
		  const struct xdcr_desc *const *sel, size_t n);

void pipeline_run(struct pipeline *p, const struct pod_frame *in,
		  const struct xdcr_desc *const *sel, size_t n, int32_t rail_mv, bool rail_valid,
		  uint16_t init_fail_bits, struct pod_report *out);

const char *pod_derived_name(enum pod_derived d);

#endif /* PRAHARI_PIPELINE_H_ */
