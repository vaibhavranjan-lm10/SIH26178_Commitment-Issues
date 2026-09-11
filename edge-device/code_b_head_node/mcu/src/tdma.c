/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <hn/tdma.h>

int tdma_layout(const struct tdma_cfg *c, struct tdma_layout *l)
{
	uint64_t pod_end, mesh_end;

	if (!c || !l || c->superframe_ms == 0 || c->n_mesh_slots == 0 || c->mesh_slot_ms == 0) {
		return -EINVAL;
	}
	pod_end = (uint64_t)c->beacon_ms + (uint64_t)c->n_pod_slots * c->pod_slot_ms;
	mesh_end = pod_end + (uint64_t)c->n_mesh_slots * c->mesh_slot_ms;
	if (mesh_end > c->superframe_ms) {
		return -EINVAL;
	}
	l->pod_region_ms = c->beacon_ms;
	l->mesh_region_ms = (uint32_t)pod_end;
	l->used_ms = (uint32_t)mesh_end;
	return 0;
}

uint32_t tdma_hash(const uint8_t *id, uint32_t len)
{
	uint32_t h = 2166136261u; /* FNV-1a */

	for (uint32_t i = 0; i < len; i++) {
		h ^= id[i];
		h *= 16777619u;
	}
	return h;
}

uint8_t tdma_mesh_slot(const uint8_t *id, uint32_t len, uint8_t n_mesh_slots)
{
	return n_mesh_slots ? (uint8_t)(tdma_hash(id, len) % n_mesh_slots) : 0;
}

uint32_t tdma_superframe_index(const struct tdma_cfg *c, uint64_t utc_ms)
{
	return (uint32_t)(utc_ms / c->superframe_ms);
}

uint32_t tdma_offset_in_superframe(const struct tdma_cfg *c, uint64_t utc_ms)
{
	return (uint32_t)(utc_ms % c->superframe_ms);
}

static uint64_t next_at_offset(const struct tdma_cfg *c, uint64_t now, uint32_t offset)
{
	uint64_t target = now + c->guard_ms;
	uint64_t sf = target / c->superframe_ms;
	uint64_t t = sf * c->superframe_ms + offset;

	if (t < target) {
		t += c->superframe_ms;
	}
	return t;
}

uint64_t tdma_next_beacon(const struct tdma_cfg *c, uint64_t now_utc_ms)
{
	return next_at_offset(c, now_utc_ms, 0);
}

uint64_t tdma_next_pod_slot_start(const struct tdma_cfg *c, uint64_t now_utc_ms, uint8_t slot)
{
	return next_at_offset(c, now_utc_ms, c->beacon_ms + (uint32_t)slot * c->pod_slot_ms);
}

uint64_t tdma_next_mesh_tx(const struct tdma_cfg *c, uint64_t now_utc_ms, uint8_t mesh_slot)
{
	struct tdma_layout l;

	(void)tdma_layout(c, &l);
	return next_at_offset(c, now_utc_ms, l.mesh_region_ms + (uint32_t)mesh_slot * c->mesh_slot_ms);
}

int tdma_pod_slot_at(const struct tdma_cfg *c, uint64_t utc_ms)
{
	uint32_t off = tdma_offset_in_superframe(c, utc_ms);
	uint32_t end = c->beacon_ms + (uint32_t)c->n_pod_slots * c->pod_slot_ms;

	if (off < c->beacon_ms || off >= end) {
		return -1;
	}
	return (int)((off - c->beacon_ms) / c->pod_slot_ms);
}

int tdma_mesh_slot_at(const struct tdma_cfg *c, uint64_t utc_ms)
{
	struct tdma_layout l;
	uint32_t off = tdma_offset_in_superframe(c, utc_ms);

	if (tdma_layout(c, &l) || off < l.mesh_region_ms || off >= l.used_ms) {
		return -1;
	}
	return (int)((off - l.mesh_region_ms) / c->mesh_slot_ms);
}
