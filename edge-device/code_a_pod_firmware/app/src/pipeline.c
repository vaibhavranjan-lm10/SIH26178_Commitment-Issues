/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <string.h>
#include <prahari/derive.h>
#include <prahari/params.h>
#include <prahari/pipeline.h>

static int slot_of(const struct pipeline *p, uint8_t param)
{
	for (uint8_t i = 0; i < p->n_slots; i++) {
		if (p->slot_param[i] == param) {
			return i;
		}
	}
	return -1;
}

int pipeline_init(struct pipeline *p, const struct pipeline_cfg *cfg,
		  const struct xdcr_desc *const *sel, size_t n)
{
	if (!p || !cfg) {
		return -EINVAL;
	}
	memset(p, 0, sizeof(*p));
	p->cfg = *cfg;
	for (size_t i = 0; i < n; i++) {
		for (uint8_t k = 0; k < sel[i]->n_params; k++) {
			uint8_t param = sel[i]->params[k];

			if (!prahari_param_is_insitu(param) || slot_of(p, param) >= 0) {
				continue;
			}
			if (p->n_slots >= PIPELINE_MAX_CHANNELS) {
				return -ENOSPC;
			}
			p->slot_param[p->n_slots] = param;
			median5_init(&p->med[p->n_slots]);
			zgate_init(&p->zg[p->n_slots]);
			p->n_slots++;
		}
	}
	return 0;
}

static const struct pod_range *range_of(const struct pipeline *p, uint8_t param)
{
	for (size_t i = 0; i < p->cfg.n_ranges; i++) {
		if (p->cfg.ranges[i].param == param) {
			return &p->cfg.ranges[i];
		}
	}
	return NULL;
}

/* One in-situ channel: calibrate -> median -> range -> z gate. */
static void process_channel(struct pipeline *p, uint8_t param, const struct pod_frame *in,
			    struct pod_report *out)
{
	uint8_t fl = in->flags[param];
	int32_t v = in->value[param];
	const struct calib_entry *ce;
	const struct pod_range *r;
	int slot;

	out->primary.flags[param] = fl;
	out->primary.value[param] = v;
	if (!(fl & XDCR_F_VALID)) {
		return; /* masked stays masked, with its reason flags */
	}

	/* 1. two-point calibration */
	ce = calib_find(p->cfg.calib, param);
	if (ce) {
		int32_t c;

		if (calib_apply(ce, v, &c) == 0) {
			v = c;
			fl &= (uint8_t)~XDCR_F_RAW;
		} else {
			fl |= XDCR_F_UNCAL;
		}
	} else if (fl & XDCR_F_RAW) {
		fl |= XDCR_F_UNCAL; /* raw ADC/pulse units, no mapping: keep but mark */
	}

	slot = slot_of(p, param);
	if ((fl & XDCR_F_UNCAL) || slot < 0) {
		out->primary.value[param] = v;
		out->primary.flags[param] = fl;
		return; /* unknown units: no filtering, gating or deriving */
	}

	/* 2. median-of-5 spike rejection (fresh samples only) */
	if (!(fl & XDCR_F_STALE)) {
		v = median5_push(&p->med[slot], v);
	} else {
		v = median5_last(&p->med[slot]);
	}

	/* 3. range gate: flag, never discard */
	r = range_of(p, param);
	if (r && (v < r->lo || v > r->hi)) {
		fl |= XDCR_F_RANGE;
	}

	/* 4. z-score anomaly gate on in-spec, fresh, filtered values */
	if (!(fl & (XDCR_F_RANGE | XDCR_F_STALE))) {
		int rc = zgate_update(&p->zg[slot], v, p->cfg.z_thr_centi,
				      p->cfg.z_floor_permille, NULL);

		if (rc == 1) {
			fl |= XDCR_F_ANOMALY;
			out->wake_params |= (uint64_t)1 << param;
			out->wake = true;
		}
	}

	out->primary.value[param] = v;
	out->primary.flags[param] = fl;
}

static bool usable(const struct pod_frame *f, uint8_t param)
{
	return (f->flags[param] & XDCR_F_VALID) && !(f->flags[param] & XDCR_F_UNCAL);
}

static uint8_t inherit(const struct pod_frame *f, uint8_t a, uint8_t b)
{
	uint8_t m = XDCR_F_RANGE | XDCR_F_STALE | XDCR_F_ANOMALY;

	return (uint8_t)(XDCR_F_VALID | ((f->flags[a] | f->flags[b]) & m));
}

static void derive_all(struct pod_report *out)
{
	const struct pod_frame *f = &out->primary;

	if (usable(f, PRAHARI_P13_AIR_TEMP_UNDERSTORY) && usable(f, PRAHARI_P15_RH_UNDERSTORY)) {
		int32_t vpd, td, hi;
		uint8_t fl = inherit(f, PRAHARI_P13_AIR_TEMP_UNDERSTORY, PRAHARI_P15_RH_UNDERSTORY);

		if (derive_vpd_dewpoint(f->value[PRAHARI_P13_AIR_TEMP_UNDERSTORY],
					f->value[PRAHARI_P15_RH_UNDERSTORY], &vpd, &td) == 0) {
			out->derived[DER_S13_VPD] = vpd;
			out->derived_flags[DER_S13_VPD] = fl;
			out->derived[DER_S14_DEW_POINT] = td;
			out->derived_flags[DER_S14_DEW_POINT] = fl;
		}
		if (derive_heat_index(f->value[PRAHARI_P13_AIR_TEMP_UNDERSTORY],
				      f->value[PRAHARI_P15_RH_UNDERSTORY], &hi) == 0) {
			out->derived[DER_S21_HEAT_INDEX] = hi;
			out->derived_flags[DER_S21_HEAT_INDEX] = fl;
		}
	}
	if (usable(f, PRAHARI_P18_WIND_SPEED) && usable(f, PRAHARI_P19_WIND_DIRECTION)) {
		int32_t u, v;
		uint8_t fl = inherit(f, PRAHARI_P18_WIND_SPEED, PRAHARI_P19_WIND_DIRECTION);

		if (derive_wind_uv(f->value[PRAHARI_P18_WIND_SPEED],
				   f->value[PRAHARI_P19_WIND_DIRECTION], &u, &v, NULL, NULL) == 0) {
			out->derived[DER_S15_WIND_U] = u;
			out->derived_flags[DER_S15_WIND_U] = fl;
			out->derived[DER_S16_WIND_V] = v;
			out->derived_flags[DER_S16_WIND_V] = fl;
		}
	}
}

void pipeline_run(struct pipeline *p, const struct pod_frame *in,
		  const struct xdcr_desc *const *sel, size_t n, int32_t rail_mv, bool rail_valid,
		  uint16_t init_fail_bits, struct pod_report *out)
{
	memset(out, 0, sizeof(*out));
	out->primary.cycle = in->cycle;

	for (uint8_t param = 1; param <= PRAHARI_PARAM_INSITU_COUNT; param++) {
		process_channel(p, param, in, out);
	}
	derive_all(out);

	/* self-report (§4.3): own rail, per-transducer fault flags */
	out->self.rail_mv = rail_mv;
	out->self.rail_valid = rail_valid;
	out->self.xdcr_fault = init_fail_bits;
	for (size_t i = 0; i < n && i < 16; i++) {
		for (uint8_t k = 0; k < sel[i]->n_params; k++) {
			uint8_t param = sel[i]->params[k];
			uint8_t fl;

			if (!prahari_param_is_insitu(param)) {
				continue;
			}
			fl = out->primary.flags[param];
			if (fl & XDCR_F_FAULT) {
				out->self.xdcr_fault |= (uint16_t)(1u << i);
			}
			if (fl & XDCR_F_NOPOWER) {
				out->self.xdcr_nopower |= (uint16_t)(1u << i);
			}
			if (fl & XDCR_F_RANGE) {
				out->self.xdcr_range |= (uint16_t)(1u << i);
			}
		}
	}
}

const char *pod_derived_name(enum pod_derived d)
{
	static const char *const names[DER_COUNT] = {"S13", "S14", "S15", "S16", "S21"};

	return d < DER_COUNT ? names[d] : "?";
}
