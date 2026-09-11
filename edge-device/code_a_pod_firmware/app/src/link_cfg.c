/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <prahari/link_cfg.h>
#include <prahari/pod_position.h> /* pod_config_crc8 */
#include <prahari/rs485.h>

int link_cfg_encode(const struct link_cfg *c, uint8_t *out, size_t len)
{
	if (!c || !out || len < LINK_CFG_BLOB_LEN || !rs485_addr_valid(c->addr)) {
		return -EINVAL;
	}
	out[0] = LINK_CFG_MAGIC0;
	out[1] = LINK_CFG_MAGIC1;
	out[2] = LINK_CFG_VERSION;
	out[3] = c->addr;
	out[4] = c->slot;
	out[5] = pod_config_crc8(out, 5);
	return 0;
}

int link_cfg_resolve(const uint8_t *blob, size_t len, const struct link_cfg *dflt,
		     struct link_cfg *out, enum link_cfg_source *src)
{
	if (!out || !dflt || !rs485_addr_valid(dflt->addr)) {
		return -EINVAL;
	}
	if (blob && len >= LINK_CFG_BLOB_LEN && blob[0] == LINK_CFG_MAGIC0 &&
	    blob[1] == LINK_CFG_MAGIC1 && blob[2] == LINK_CFG_VERSION &&
	    blob[5] == pod_config_crc8(blob, 5) && rs485_addr_valid(blob[3])) {
		out->addr = blob[3];
		out->slot = blob[4];
		if (src) {
			*src = LINK_CFG_SRC_EEPROM;
		}
		return 0;
	}
	*out = *dflt;
	if (src) {
		*src = LINK_CFG_SRC_KCONFIG;
	}
	return 0;
}
