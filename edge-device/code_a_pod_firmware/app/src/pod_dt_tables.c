/*
 * Devicetree -> firmware tables.
 *
 * /load-switches/<n>  (prahari,load-switch)      -> pod_pwr_domains[idx]
 * /transducers/<n>    (prahari,*-transducer)     -> pod_xdcrs[idx]
 *
 * Adding a transducer to a position is a devicetree edit: a node here
 * with prahari,params / prahari,positions / power-domain.  No C changes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/util.h>
#include <zephyr/toolchain.h>
#include <prahari/params.h>
#include "drivers/xdcr_drivers.h"
#include "pod_dt_tables.h"

/* ------------------------------------------------------------------ */
/* Load switches (blueprint §3.4)                                      */
/* ------------------------------------------------------------------ */

#define LS_CFG(n)                                                                                  \
	[DT_NODE_CHILD_IDX(n)] = {                                                                 \
		.name = DT_NODE_FULL_NAME(n),                                                      \
		.policy = (enum pwr_policy)DT_ENUM_IDX(n, policy),                                 \
		.current_ma = DT_PROP(n, current_ma),                                              \
		.settle_ms = DT_PROP(n, settle_ms),                                                \
		.on_ms = DT_PROP(n, on_ms),                                                        \
		.period_ms = DT_PROP(n, period_ms),                                                \
	},

const struct pwr_domain_cfg pod_pwr_domains[POD_PWR_DOMAIN_COUNT] = {
	DT_FOREACH_CHILD(POD_LS_NODE, LS_CFG)};

struct pwr_domain_state pod_pwr_states[POD_PWR_DOMAIN_COUNT];

#define LS_GPIO(n) [DT_NODE_CHILD_IDX(n)] = GPIO_DT_SPEC_GET(n, gpios),

static const struct gpio_dt_spec ls_gpios[POD_PWR_DOMAIN_COUNT] = {
	DT_FOREACH_CHILD(POD_LS_NODE, LS_GPIO)};

static int ls_set(void *ctx, uint8_t dom, bool on)
{
	ARG_UNUSED(ctx);
	if (dom >= POD_PWR_DOMAIN_COUNT) {
		return -EINVAL;
	}
	return gpio_pin_set_dt(&ls_gpios[dom], on ? 1 : 0);
}

const struct pwr_backend pod_pwr_backend = {.set = ls_set, .ctx = NULL};

int pod_pwr_gpio_init(void)
{
	for (uint8_t d = 0; d < POD_PWR_DOMAIN_COUNT; d++) {
		int rc;

		if (!gpio_is_ready_dt(&ls_gpios[d])) {
			return -ENODEV;
		}
		rc = gpio_pin_configure_dt(&ls_gpios[d], GPIO_OUTPUT_INACTIVE);
		if (rc) {
			return rc;
		}
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/* Transducers (blueprint §3.1 classes, §4.1 positions)                 */
/* ------------------------------------------------------------------ */

#define XD_PARAMS(n) UTIL_CAT(xd_params_, DT_NODE_CHILD_IDX(n))
#define XD_CTX(n) UTIL_CAT(xd_ctx_, DT_NODE_CHILD_IDX(n))

#define XD_IS_ADC(n) DT_NODE_HAS_COMPAT(n, prahari_adc_transducer)
#define XD_IS_PULSE(n) DT_NODE_HAS_COMPAT(n, prahari_pulse_transducer)
#define XD_IS_SENSOR(n) DT_NODE_HAS_COMPAT(n, prahari_sensor_transducer)

/* Parameter ID arrays.  Every ID must be a PRIMARY in-situ P-ID. */
#define XD_PARAM_CHECK(n, prop, idx)                                                               \
	BUILD_ASSERT(DT_PROP_BY_IDX(n, prop, idx) >= 1 &&                                          \
			     DT_PROP_BY_IDX(n, prop, idx) <= PRAHARI_PARAM_INSITU_COUNT,           \
		     "prahari,params: not a P1..P46 ID from prahari_parameters.md");
#define XD_PARAMS_DEF(n)                                                                           \
	static const uint8_t XD_PARAMS(n)[] = DT_PROP(n, prahari_params);                          \
	DT_FOREACH_PROP_ELEM(n, prahari_params, XD_PARAM_CHECK)

DT_FOREACH_CHILD(POD_XD_NODE, XD_PARAMS_DEF)

/* Position mask from the "prahari,positions" string-array. */
#define POS_BIT(n, prop, idx)                                                                      \
	POD_POS_BIT(UTIL_CAT(POD_POS_, DT_STRING_UPPER_TOKEN_BY_IDX(n, prop, idx)))
#define XD_POS_MASK(n) (DT_FOREACH_PROP_ELEM_SEP(n, prahari_positions, POS_BIT, (|)))

/* Power-domain index from the phandle; absent = passive (rain gauge). */
#define XD_DOMAIN(n)                                                                               \
	COND_CODE_1(DT_NODE_HAS_PROP(n, power_domain),                                             \
		    (DT_NODE_CHILD_IDX(DT_PHANDLE(n, power_domain))), (XDCR_DOMAIN_NONE))

/* Class C */
#define ADC_SPEC_ELEM(n, prop, idx) ADC_DT_SPEC_GET_BY_IDX(n, idx)
#define XD_ADC_CTX_DEF(n)                                                                          \
	static const struct xdcr_adc_ctx XD_CTX(n) = {                                             \
		.spec = {DT_FOREACH_PROP_ELEM_SEP(n, io_channels, ADC_SPEC_ELEM, (,))},            \
		.n_ch = DT_PROP_LEN(n, io_channels),                                                  \
	};                                                                                         \
	BUILD_ASSERT(DT_PROP_LEN(n, io_channels) == DT_PROP_LEN(n, prahari_params),                \
		     "adc transducer: one io-channel per parameter");                              \
	BUILD_ASSERT(DT_PROP_LEN(n, io_channels) <= XDCR_ADC_MAX_CH, "too many ADC channels");

/* Class D */
#define XD_PULSE_CTX_DEF(n)                                                                        \
	static struct xdcr_pulse_ctx XD_CTX(n) = {                                                 \
		.gpio = GPIO_DT_SPEC_GET(n, gpios),                                                \
		.debounce_ms = DT_PROP(n, debounce_ms),                                            \
		.mode = DT_ENUM_IDX(n, mode),                                                      \
	};                                                                                         \
	BUILD_ASSERT(DT_PROP_LEN(n, prahari_params) == 1,                                          \
		     "pulse transducer reports exactly one parameter");

/* Class A / B */
#define SENSOR_CHAN_ELEM(n, prop, idx)                                                             \
	UTIL_CAT(SENSOR_CHAN_, DT_STRING_UPPER_TOKEN_BY_IDX(n, prop, idx))
#define XD_SENSOR_CTX_DEF(n)                                                                       \
	static const struct xdcr_sensor_ctx XD_CTX(n) = {                                          \
		.dev = DEVICE_DT_GET(DT_PHANDLE(n, sensor)),                                       \
		.chan = {DT_FOREACH_PROP_ELEM_SEP(n, sensor_channels, SENSOR_CHAN_ELEM, (,))},     \
		.n_ch = DT_PROP_LEN(n, sensor_channels),                                              \
	};                                                                                         \
	BUILD_ASSERT(DT_PROP_LEN(n, sensor_channels) == DT_PROP_LEN(n, prahari_params),            \
		     "sensor transducer: one sensor channel per parameter");                       \
	BUILD_ASSERT(DT_PROP_LEN(n, sensor_channels) <= XDCR_SENSOR_MAX_CH,                        \
		     "too many sensor channels");

#define XD_CTX_DEF(n)                                                                              \
	COND_CODE_1(XD_IS_ADC(n), (XD_ADC_CTX_DEF(n)), ())                                         \
	COND_CODE_1(XD_IS_PULSE(n), (XD_PULSE_CTX_DEF(n)), ())                                     \
	COND_CODE_1(XD_IS_SENSOR(n), (XD_SENSOR_CTX_DEF(n)), ())                                   \
	BUILD_ASSERT(XD_IS_ADC(n) + XD_IS_PULSE(n) + XD_IS_SENSOR(n) == 1,                          \
		     "transducer node must have exactly one prahari,*-transducer compatible");

DT_FOREACH_CHILD(POD_XD_NODE, XD_CTX_DEF)

#define XD_CLASS_OPS(n)                                                                            \
	COND_CODE_1(XD_IS_ADC(n),                                                                  \
		    (.cls = XDCR_CLASS_C_ADC, .ops = &xdcr_adc_ops, .ctx = (void *)&XD_CTX(n),),  \
		    ())                                                                            \
	COND_CODE_1(XD_IS_PULSE(n),                                                                \
		    (.cls = XDCR_CLASS_D_PULSE, .ops = &xdcr_pulse_ops, .ctx = (void *)&XD_CTX(n),), \
		    ())                                                                            \
	COND_CODE_1(XD_IS_SENSOR(n),                                                               \
		    (.cls = (enum xdcr_class)DT_ENUM_IDX(n, class), .ops = &xdcr_sensor_ops,       \
		     .ctx = (void *)&XD_CTX(n),),                                                  \
		    ())

#define XD_ENTRY(n)                                                                                \
	[DT_NODE_CHILD_IDX(n)] = {                                                                 \
		.name = DT_NODE_FULL_NAME(n),                                                      \
		.position_mask = COND_CODE_1(DT_NODE_HAS_STATUS_OKAY(n), (XD_POS_MASK(n)), (0)),   \
		.domain = XD_DOMAIN(n),                                                            \
		.n_params = DT_PROP_LEN(n, prahari_params),                                        \
		.params = XD_PARAMS(n),                                                            \
		XD_CLASS_OPS(n)},

const struct xdcr_desc pod_xdcrs[POD_XDCR_COUNT] = {DT_FOREACH_CHILD(POD_XD_NODE, XD_ENTRY)};

/* ------------------------------------------------------------------ */
/* Range gate bounds (blueprint §4.3: flag out-of-spec, never discard)   */
/* ------------------------------------------------------------------ */

#define XD_RANGE_ENTRY(n, prop, idx)                                                               \
	{.param = DT_PROP_BY_IDX(n, prahari_params, idx),                                          \
	 .lo = (int32_t)DT_PROP_BY_IDX(n, prahari_range_lo, idx),                                  \
	 .hi = (int32_t)DT_PROP_BY_IDX(n, prahari_range_hi, idx)},
#define XD_RANGES(n)                                                                               \
	COND_CODE_1(DT_NODE_HAS_PROP(n, prahari_range_lo),                                         \
		    (DT_FOREACH_PROP_ELEM(n, prahari_params, XD_RANGE_ENTRY)), ())
#define XD_RANGE_CHECK(n)                                                                          \
	COND_CODE_1(DT_NODE_HAS_PROP(n, prahari_range_lo),                                         \
		    (BUILD_ASSERT(DT_PROP_LEN(n, prahari_range_lo) == DT_PROP_LEN(n, prahari_params) && \
				  DT_PROP_LEN(n, prahari_range_hi) == DT_PROP_LEN(n, prahari_params), \
				  "prahari,range-lo/hi must have one entry per parameter");),      \
		    ())

DT_FOREACH_CHILD(POD_XD_NODE, XD_RANGE_CHECK)

const struct pod_range pod_ranges[] = {DT_FOREACH_CHILD(POD_XD_NODE, XD_RANGES){.param = 0}};
const size_t pod_range_count = ARRAY_SIZE(pod_ranges) - 1;
