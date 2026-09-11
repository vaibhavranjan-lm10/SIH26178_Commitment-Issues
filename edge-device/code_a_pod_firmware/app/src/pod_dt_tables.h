/* Tables generated from devicetree: load switches and transducers.
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef PRAHARI_POD_DT_TABLES_H_
#define PRAHARI_POD_DT_TABLES_H_

#include <stddef.h>
#include <zephyr/devicetree.h>
#include <prahari/power.h>
#include <prahari/pipeline.h>
#include <prahari/xdcr.h>

#define POD_LS_NODE DT_PATH(load_switches)
#define POD_XD_NODE DT_PATH(transducers)
#define POD_PWR_DOMAIN_COUNT DT_CHILD_NUM(POD_LS_NODE)
#define POD_XDCR_COUNT DT_CHILD_NUM(POD_XD_NODE)

extern const struct pwr_domain_cfg pod_pwr_domains[POD_PWR_DOMAIN_COUNT];
extern struct pwr_domain_state pod_pwr_states[POD_PWR_DOMAIN_COUNT];
extern const struct pwr_backend pod_pwr_backend;
int pod_pwr_gpio_init(void);

extern const struct xdcr_desc pod_xdcrs[POD_XDCR_COUNT];

/* Range-gate table from prahari,range-lo/hi; terminated by param 0. */
struct pod_range;
extern const struct pod_range pod_ranges[];
extern const size_t pod_range_count;

#endif /* PRAHARI_POD_DT_TABLES_H_ */
