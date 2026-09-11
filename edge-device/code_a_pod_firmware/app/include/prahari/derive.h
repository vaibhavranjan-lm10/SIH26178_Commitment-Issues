/*
 * Instantaneous derivations (blueprint §4.3).  Each takes only this
 * cycle's primaries — no history.  Milli-units in, milli-units out.
 *   S13 VPD        (m-kPa)   from P13 (m-degC), P15 (m-%RH)
 *   S14 dew point  (m-degC)  from P13, P15
 *   S15/S16 u, v   (m-m/s)   from P18 (m-m/s), P19 (m-deg, "from" bearing)
 *   S21 heat index (m-degC)  from P13, P15
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef PRAHARI_DERIVE_H_
#define PRAHARI_DERIVE_H_

#include <stdint.h>

/* Magnus (Alduchov-Eskridge 1996): es = 0.61094 exp(17.625 T / (T + 243.04)) kPa */
int derive_vpd_dewpoint(int32_t t_mc, int32_t rh_mpct, int32_t *vpd_mkpa, int32_t *td_mc);

/* Meteorological convention: u = -s sin(dir), v = -s cos(dir).  Also
 * returns the unit direction vector (sin, cos) scaled by 1000 so a
 * bearing never leaves this function as a bare angle. */
int derive_wind_uv(int32_t speed_mms, int32_t dir_mdeg, int32_t *u_mms, int32_t *v_mms,
		   int32_t *sin_milli, int32_t *cos_milli);

/* NWS Rothfusz regression with the Steadman low-range form and the two
 * standard humidity adjustments. */
int derive_heat_index(int32_t t_mc, int32_t rh_mpct, int32_t *hi_mc);

#endif /* PRAHARI_DERIVE_H_ */
