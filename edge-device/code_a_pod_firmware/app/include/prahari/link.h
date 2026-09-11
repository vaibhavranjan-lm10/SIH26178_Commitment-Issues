/*
 * Head-node link, one of the two blueprint §4.4 modes chosen at build
 * time (Kconfig PRAHARI_LINK_MODE_W / _R — never both in one image):
 *   Mode W  RS-485 polled slave           src/link_w.c
 *   Mode R  LoRa SX1262 scheduled slot    src/link_r.c
 * Both expose this interface to main.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef PRAHARI_LINK_H_
#define PRAHARI_LINK_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <prahari/pipeline.h>

#define PRAHARI_FW_MAJOR 0
#define PRAHARI_FW_MINOR 1

struct link_identity {
	uint8_t addr;     /* RS-485 address (Mode W) */
	uint8_t slot;     /* TDMA slot (Mode R) */
	uint8_t position; /* pod position, carried in every report */
};

typedef void (*link_arm_cb_t)(void *ctx, bool armed);

int link_init(const struct link_identity *id, link_arm_cb_t arm_cb, void *ctx);
/* Hand the latest processed report to the link (copied). */
int link_publish(const struct pod_report *r);
/* Drive the link state machine; call from the main loop. */
void link_service(uint32_t now_ms);
/* Block until link activity or timeout (lets the main loop react to a poll). */
void link_wait(k_timeout_t timeout);
const char *link_mode_name(void);

#endif /* PRAHARI_LINK_H_ */
