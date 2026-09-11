/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <string.h>
#include <prahari/pod_position.h>

uint8_t pod_config_crc8(const uint8_t *data, size_t len)
{
	uint8_t crc = 0;

	for (size_t i = 0; i < len; i++) {
		crc ^= data[i];
		for (int b = 0; b < 8; b++) {
			crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
		}
	}
	return crc;
}

int pod_config_blob_encode(enum pod_position pos, uint8_t *out, size_t len)
{
	if (out == NULL || len < POD_CFG_BLOB_LEN || pos >= POD_POS_COUNT) {
		return -EINVAL;
	}
	out[0] = POD_CFG_MAGIC0;
	out[1] = POD_CFG_MAGIC1;
	out[2] = POD_CFG_VERSION;
	out[3] = (uint8_t)pos;
	out[4] = pod_config_crc8(out, 4);
	return 0;
}

static int blob_is_blank(const uint8_t *blob)
{
	for (size_t i = 0; i < POD_CFG_BLOB_LEN; i++) {
		if (blob[i] != 0xFF) {
			return 0;
		}
	}
	return 1;
}

int pod_position_resolve(const uint8_t *blob, size_t len, enum pod_position dflt,
			 enum pod_position *out, enum pod_position_source *src)
{
	if (out == NULL || dflt >= POD_POS_COUNT) {
		return -EINVAL;
	}

	if (blob != NULL && len >= POD_CFG_BLOB_LEN && !blob_is_blank(blob) &&
	    blob[0] == POD_CFG_MAGIC0 && blob[1] == POD_CFG_MAGIC1 &&
	    blob[2] == POD_CFG_VERSION && blob[4] == pod_config_crc8(blob, 4) &&
	    blob[3] < POD_POS_COUNT) {
		*out = (enum pod_position)blob[3];
		if (src) {
			*src = POD_POS_SRC_EEPROM;
		}
		return 0;
	}

	*out = dflt;
	if (src) {
		*src = POD_POS_SRC_KCONFIG;
	}
	return 0;
}

const char *pod_position_name(enum pod_position pos)
{
	static const char *const names[POD_POS_COUNT] = {"S1", "S2", "S3", "G", "U", "C"};

	return pos < POD_POS_COUNT ? names[pos] : "?";
}
