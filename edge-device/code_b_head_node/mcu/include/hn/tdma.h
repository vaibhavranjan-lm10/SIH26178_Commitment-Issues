/*
 * LoRa TDMA timing for the head node (blueprint §7.3/§7.4, §4.4).
 *
 * One superframe per inference cycle, aligned to UTC so every head node
 * in a mesh derives the same boundaries from GNSS time with no
 * coordinator.  Layout inside a superframe:
 *
 *   [0, beacon)                       pod beacon (this node -> its Mode R pods)
 *   [beacon, beacon + n_pod*pod_slot) pod uplink slots (pods -> this node)
 *   [mesh0, mesh0 + n_mesh*mesh_slot) mesh slots, one per head node,
 *                                     slot = hash(node_id) mod n_mesh
 *
 * Pure; all times are UTC milliseconds (64-bit) from the PPS-disciplined
 * clock, or local ms when the node is in holdover.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef HN_TDMA_H_
#define HN_TDMA_H_

#include <stdbool.h>
#include <stdint.h>

struct tdma_cfg {
	uint32_t superframe_ms;
	uint16_t beacon_ms;
	uint16_t pod_slot_ms;
	uint8_t n_pod_slots;
	uint16_t mesh_slot_ms;
	uint8_t n_mesh_slots;
	uint16_t guard_ms;
};

struct tdma_layout {
	uint32_t pod_region_ms;  /* offset of first pod slot */
	uint32_t mesh_region_ms; /* offset of first mesh slot */
	uint32_t used_ms;        /* end of the last mesh slot */
};

/* -EINVAL if the regions do not fit the superframe. */
int tdma_layout(const struct tdma_cfg *c, struct tdma_layout *l);

/* FNV-1a 32-bit hash of the node id; §7.4 "slot assignment from node ID hash". */
uint32_t tdma_hash(const uint8_t *id, uint32_t len);
uint8_t tdma_mesh_slot(const uint8_t *id, uint32_t len, uint8_t n_mesh_slots);

/* Superframe index and offset of a UTC instant. */
uint32_t tdma_superframe_index(const struct tdma_cfg *c, uint64_t utc_ms);
uint32_t tdma_offset_in_superframe(const struct tdma_cfg *c, uint64_t utc_ms);

/* Next instant >= now + guard for each event. */
uint64_t tdma_next_beacon(const struct tdma_cfg *c, uint64_t now_utc_ms);
uint64_t tdma_next_pod_slot_start(const struct tdma_cfg *c, uint64_t now_utc_ms, uint8_t slot);
uint64_t tdma_next_mesh_tx(const struct tdma_cfg *c, uint64_t now_utc_ms, uint8_t mesh_slot);

/* Are we inside the pod uplink region right now?  Returns the slot index or -1. */
int tdma_pod_slot_at(const struct tdma_cfg *c, uint64_t utc_ms);
/* Same for mesh slots; -1 if outside. */
int tdma_mesh_slot_at(const struct tdma_cfg *c, uint64_t utc_ms);

#endif /* HN_TDMA_H_ */
