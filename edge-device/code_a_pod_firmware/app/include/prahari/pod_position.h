/*
 * Pod position (blueprint §4.1) and its boot-time resolution.
 * Hardware-independent; unit-tested on native_sim.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef PRAHARI_POD_POSITION_H_
#define PRAHARI_POD_POSITION_H_

#include <stddef.h>
#include <stdint.h>

/* Order is load-bearing: devicetree "prahari,positions" strings are turned
 * into POD_POS_<TOKEN> at compile time. */
enum pod_position {
	POD_POS_S1 = 0, /* -10 cm */
	POD_POS_S2,     /* -40 cm */
	POD_POS_S3,     /* -100 cm */
	POD_POS_G,      /* ground, 0 m */
	POD_POS_U,      /* understory, ~2 m */
	POD_POS_C,      /* canopy / mast top, ~10 m */
	POD_POS_COUNT,
};

#define POD_POS_BIT(p) (1u << (p))

enum pod_position_source {
	POD_POS_SRC_EEPROM,
	POD_POS_SRC_KCONFIG,
};

/* Persistent config block at EEPROM offset 0. */
#define POD_CFG_MAGIC0   'P'
#define POD_CFG_MAGIC1   'D'
#define POD_CFG_VERSION  1
#define POD_CFG_BLOB_LEN 5 /* magic0, magic1, version, position, crc8 */

/* CRC-8, polynomial 0x07, init 0x00 (SMBus PEC style). */
uint8_t pod_config_crc8(const uint8_t *data, size_t len);

/* Encode a config block for provisioning.  Returns 0 or -EINVAL. */
int pod_config_blob_encode(enum pod_position pos, uint8_t *out, size_t len);

/*
 * Resolve the position for this boot.
 *
 * blob/len: EEPROM contents (NULL or short when unavailable).  A block is
 * used only if magic, version, CRC and range all check out; anything
 * else falls back to 'dflt' (the Kconfig choice).  Returns 0, or -EINVAL
 * if even the default is out of range.
 */
int pod_position_resolve(const uint8_t *blob, size_t len, enum pod_position dflt,
			 enum pod_position *out, enum pod_position_source *src);

const char *pod_position_name(enum pod_position pos);

#endif /* PRAHARI_POD_POSITION_H_ */
