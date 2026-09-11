/*
 * PRAHARI parameter IDs — Code A (pod firmware).
 *
 * Every ID here traces 1:1 to docs/reference/prahari_parameters.md v2.0.
 * Only the tiers a pod can produce are listed: PRIMARY in situ (P1..P46)
 * and OPERATIONAL (O1..O8).  Satellite, secondary and tertiary parameters
 * never exist at L1.  Do not add an ID that is not in the reference doc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef PRAHARI_PARAMS_H_
#define PRAHARI_PARAMS_H_

#include <stdint.h>

enum prahari_param {
	PRAHARI_P_NONE = 0,

	/* Soil (10) */
	PRAHARI_P1_SOIL_VWC_10CM = 1,        /* m3/m3  * */
	PRAHARI_P2_SOIL_VWC_40CM = 2,        /* m3/m3  * */
	PRAHARI_P3_SOIL_VWC_100CM = 3,       /* m3/m3  * */
	PRAHARI_P4_SOIL_TEMP_10CM = 4,       /* degC */
	PRAHARI_P5_SOIL_TEMP_40CM = 5,       /* degC */
	PRAHARI_P6_SOIL_TEMP_100CM = 6,      /* degC */
	PRAHARI_P7_SOIL_EC_10CM = 7,         /* dS/m */
	PRAHARI_P8_SOIL_EC_40CM = 8,         /* dS/m */
	PRAHARI_P9_SOIL_EC_100CM = 9,        /* dS/m */
	PRAHARI_P10_SURFACE_SOIL_MOISTURE = 10, /* m3/m3, 0-5 cm  * */

	/* Surface hydrology (2) */
	PRAHARI_P11_WATER_LEVEL = 11,        /* m  * */
	PRAHARI_P12_RAINFALL_ACCUM = 12,     /* mm, ACCUMULATION not rate  * */

	/* Atmosphere (8) */
	PRAHARI_P13_AIR_TEMP_UNDERSTORY = 13, /* degC  * */
	PRAHARI_P14_AIR_TEMP_CANOPY = 14,     /* degC */
	PRAHARI_P15_RH_UNDERSTORY = 15,       /* %  * */
	PRAHARI_P16_RH_CANOPY = 16,           /* % */
	PRAHARI_P17_BARO_PRESSURE = 17,       /* hPa  * */
	PRAHARI_P18_WIND_SPEED = 18,          /* m/s  * */
	PRAHARI_P19_WIND_DIRECTION = 19,      /* deg (as sin/cos pair downstream)  * */
	PRAHARI_P20_SOLAR_IRRADIANCE = 20,    /* W/m2  * */

	/* Air quality — ambient (11) */
	PRAHARI_P21_PM1_0 = 21,               /* ug/m3 */
	PRAHARI_P22_PM2_5 = 22,               /* ug/m3  * */
	PRAHARI_P23_PM10 = 23,                /* ug/m3  * */
	PRAHARI_P24_CO = 24,                  /* ppm */
	PRAHARI_P25_NO2 = 25,                 /* ppb */
	PRAHARI_P26_O3 = 26,                  /* ppb */
	PRAHARI_P27_SO2 = 27,                 /* ppb */
	PRAHARI_P28_NH3 = 28,                 /* ppb */
	PRAHARI_P29_BENZENE = 29,             /* ug/m3 */
	PRAHARI_P30_CO2 = 30,                 /* ppm */
	PRAHARI_P31_TVOC = 31,                /* index */

	/* Fire (2) */
	PRAHARI_P32_SKIN_TEMP = 32,           /* degC  * */
	PRAHARI_P33_SMOKE_OBSCURATION = 33,   /* index  * */

	/* Industrial gas leak (3) */
	PRAHARI_P34_METHANE = 34,             /* ppm */
	PRAHARI_P35_COMBUSTIBLE_LEL = 35,     /* % LEL */
	PRAHARI_P36_H2S = 36,                 /* ppm */

	/* Water quality (5) */
	PRAHARI_P37_PH = 37,
	PRAHARI_P38_TURBIDITY = 38,           /* NTU */
	PRAHARI_P39_DISSOLVED_O2 = 39,        /* mg/L */
	PRAHARI_P40_CONDUCTIVITY = 40,        /* uS/cm */
	PRAHARI_P41_WATER_TEMP = 41,          /* degC */

	/* Geotechnical (5) */
	PRAHARI_P42_TILT_X = 42,              /* mg */
	PRAHARI_P43_TILT_Y = 43,              /* mg */
	PRAHARI_P44_TILT_Z = 44,              /* mg */
	PRAHARI_P45_PORE_PRESSURE = 45,       /* kPa */
	PRAHARI_P46_SOIL_HEAT_FLUX = 46,      /* W/m2 */

	PRAHARI_P_INSITU_LAST = PRAHARI_P46_SOIL_HEAT_FLUX,

	/* OPERATIONAL — node health (8).  Offset so they never collide with P-IDs. */
	PRAHARI_O1_BATTERY_VOLTAGE = 101,
	PRAHARI_O2_BATTERY_SOC = 102,
	PRAHARI_O3_SOLAR_CURRENT = 103,
	PRAHARI_O4_NODE_INTERNAL_TEMP = 104,
	PRAHARI_O5_LINK_RSSI = 105,
	PRAHARI_O6_LINK_SNR = 106,
	PRAHARI_O7_PACKET_DELIVERY_RATIO = 107,
	PRAHARI_O8_CAL_DRIFT_FLAGS = 108,
};

#define PRAHARI_PARAM_INSITU_COUNT 46
#define PRAHARI_PARAM_OPERATIONAL_COUNT 8

_Static_assert(PRAHARI_P_INSITU_LAST == PRAHARI_PARAM_INSITU_COUNT,
	       "in-situ parameter count must match prahari_parameters.md (46)");
_Static_assert(PRAHARI_O8_CAL_DRIFT_FLAGS - PRAHARI_O1_BATTERY_VOLTAGE + 1 ==
		       PRAHARI_PARAM_OPERATIONAL_COUNT,
	       "operational parameter count must match prahari_parameters.md (8)");

static inline int prahari_param_is_insitu(uint8_t p)
{
	return p >= 1 && p <= PRAHARI_PARAM_INSITU_COUNT;
}

#endif /* PRAHARI_PARAMS_H_ */
