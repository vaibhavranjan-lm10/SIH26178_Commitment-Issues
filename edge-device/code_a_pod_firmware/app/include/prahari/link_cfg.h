/*
 * Per-pod link identity, provisioned in EEPROM next to the position
 * block: bus address (Mode W) and TDMA slot (Mode R).
 * Block at EEPROM offset LINK_CFG_EEPROM_OFFSET: 'L' 'K' ver addr slot crc8.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef PRAHARI_LINK_CFG_H_
#define PRAHARI_LINK_CFG_H_

#include <stddef.h>
#include <stdint.h>

#define LINK_CFG_MAGIC0 'L'
#define LINK_CFG_MAGIC1 'K'
#define LINK_CFG_VERSION 1
#define LINK_CFG_BLOB_LEN 6
#define LINK_CFG_EEPROM_OFFSET 8

struct link_cfg {
	uint8_t addr; /* RS-485 address 1..247 */
	uint8_t slot; /* LoRa TDMA slot index */
};

enum link_cfg_source {
	LINK_CFG_SRC_EEPROM,
	LINK_CFG_SRC_KCONFIG,
};

int link_cfg_encode(const struct link_cfg *c, uint8_t *out, size_t len);
int link_cfg_resolve(const uint8_t *blob, size_t len, const struct link_cfg *dflt,
		     struct link_cfg *out, enum link_cfg_source *src);

#endif /* PRAHARI_LINK_CFG_H_ */
