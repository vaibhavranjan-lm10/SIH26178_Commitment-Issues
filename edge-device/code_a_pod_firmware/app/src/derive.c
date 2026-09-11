/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <math.h>
#include <prahari/derive.h>

#define MAGNUS_A 17.625f
#define MAGNUS_B 243.04f
#define MAGNUS_ES0_KPA 0.61094f
#define PI_F 3.14159265358979f

static int32_t roundf_i32(float v)
{
	return (int32_t)(v >= 0 ? v + 0.5f : v - 0.5f);
}

int derive_vpd_dewpoint(int32_t t_mc, int32_t rh_mpct, int32_t *vpd_mkpa, int32_t *td_mc)
{
	float t = t_mc / 1000.0f;
	float rh = rh_mpct / 1000.0f;
	float es, ea, gamma, td;

	if (t <= -MAGNUS_B) {
		return -EINVAL;
	}
	if (rh < 0.1f) {
		rh = 0.1f; /* log(0) guard; RH below 0.1 % is not physical */
	} else if (rh > 100.0f) {
		rh = 100.0f;
	}
	es = MAGNUS_ES0_KPA * expf(MAGNUS_A * t / (t + MAGNUS_B));
	ea = es * rh / 100.0f;
	gamma = logf(rh / 100.0f) + MAGNUS_A * t / (t + MAGNUS_B);
	td = MAGNUS_B * gamma / (MAGNUS_A - gamma);

	if (vpd_mkpa) {
		*vpd_mkpa = roundf_i32((es - ea) * 1000.0f);
	}
	if (td_mc) {
		*td_mc = roundf_i32(td * 1000.0f);
	}
	return 0;
}

int derive_wind_uv(int32_t speed_mms, int32_t dir_mdeg, int32_t *u_mms, int32_t *v_mms,
		   int32_t *sin_milli, int32_t *cos_milli)
{
	float s = speed_mms / 1000.0f;
	float th = (dir_mdeg / 1000.0f) * (PI_F / 180.0f);
	float sn = sinf(th), cs = cosf(th);

	if (speed_mms < 0) {
		return -EINVAL;
	}
	if (u_mms) {
		*u_mms = roundf_i32(-s * sn * 1000.0f);
	}
	if (v_mms) {
		*v_mms = roundf_i32(-s * cs * 1000.0f);
	}
	if (sin_milli) {
		*sin_milli = roundf_i32(sn * 1000.0f);
	}
	if (cos_milli) {
		*cos_milli = roundf_i32(cs * 1000.0f);
	}
	return 0;
}

int derive_heat_index(int32_t t_mc, int32_t rh_mpct, int32_t *hi_mc)
{
	float tf = (t_mc / 1000.0f) * 9.0f / 5.0f + 32.0f;
	float rh = rh_mpct / 1000.0f;
	float hi;

	if (!hi_mc) {
		return -EINVAL;
	}
	if (rh < 0.0f) {
		rh = 0.0f;
	} else if (rh > 100.0f) {
		rh = 100.0f;
	}

	/* Steadman simple form; NWS uses it below HI 80 F. */
	hi = 0.5f * (tf + 61.0f + (tf - 68.0f) * 1.2f + rh * 0.094f);
	if (hi >= 80.0f) {
		float t2 = tf * tf, r2 = rh * rh;

		hi = -42.379f + 2.04901523f * tf + 10.14333127f * rh - 0.22475541f * tf * rh -
		     6.83783e-3f * t2 - 5.481717e-2f * r2 + 1.22874e-3f * t2 * rh +
		     8.5282e-4f * tf * r2 - 1.99e-6f * t2 * r2;
		if (rh < 13.0f && tf >= 80.0f && tf <= 112.0f) {
			float a = fabsf(tf - 95.0f);

			hi -= ((13.0f - rh) / 4.0f) * sqrtf((17.0f - a) / 17.0f);
		} else if (rh > 85.0f && tf >= 80.0f && tf <= 87.0f) {
			hi += ((rh - 85.0f) / 10.0f) * ((87.0f - tf) / 5.0f);
		}
	}
	*hi_mc = roundf_i32((hi - 32.0f) * 5.0f / 9.0f * 1000.0f);
	return 0;
}
