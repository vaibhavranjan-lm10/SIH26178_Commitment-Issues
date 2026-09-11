/* SPDX-License-Identifier: Apache-2.0 */
#include <prahari/xdcr.h>

size_t xdcr_select(const struct xdcr_desc *all, size_t n, enum pod_position pos,
		   const struct xdcr_desc **out, size_t max)
{
	size_t count = 0;

	if (pos >= POD_POS_COUNT) {
		return 0;
	}
	for (size_t i = 0; i < n; i++) {
		if (all[i].position_mask & POD_POS_BIT(pos)) {
			if (count < max) {
				out[count] = &all[i];
			}
			count++;
		}
	}
	return count;
}

const char *xdcr_class_name(enum xdcr_class cls)
{
	switch (cls) {
	case XDCR_CLASS_A_I2C:
		return "A/I2C";
	case XDCR_CLASS_B_UART:
		return "B/UART";
	case XDCR_CLASS_C_ADC:
		return "C/ADC";
	case XDCR_CLASS_D_PULSE:
		return "D/pulse";
	default:
		return "?";
	}
}
