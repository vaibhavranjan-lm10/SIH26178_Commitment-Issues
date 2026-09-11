/* Boot-time position selection (blueprint §4.1: one design, six positions). */
#include <zephyr/ztest.h>
#include <prahari/pod_position.h>

ZTEST_SUITE(pod_position, NULL, NULL, NULL, NULL, NULL);

ZTEST(pod_position, test_valid_eeprom_block_wins)
{
	uint8_t blob[POD_CFG_BLOB_LEN];
	enum pod_position pos;
	enum pod_position_source src;

	zassert_ok(pod_config_blob_encode(POD_POS_U, blob, sizeof(blob)));
	zassert_ok(pod_position_resolve(blob, sizeof(blob), POD_POS_S1, &pos, &src));
	zassert_equal(pos, POD_POS_U);
	zassert_equal(src, POD_POS_SRC_EEPROM);
}

ZTEST(pod_position, test_blank_eeprom_falls_back_to_kconfig)
{
	uint8_t blob[POD_CFG_BLOB_LEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
	enum pod_position pos;
	enum pod_position_source src;

	zassert_ok(pod_position_resolve(blob, sizeof(blob), POD_POS_G, &pos, &src));
	zassert_equal(pos, POD_POS_G);
	zassert_equal(src, POD_POS_SRC_KCONFIG);
}

ZTEST(pod_position, test_no_eeprom_falls_back_to_kconfig)
{
	enum pod_position pos;
	enum pod_position_source src;

	zassert_ok(pod_position_resolve(NULL, 0, POD_POS_C, &pos, &src));
	zassert_equal(pos, POD_POS_C);
	zassert_equal(src, POD_POS_SRC_KCONFIG);
}

ZTEST(pod_position, test_bad_crc_rejected)
{
	uint8_t blob[POD_CFG_BLOB_LEN];
	enum pod_position pos;
	enum pod_position_source src;

	zassert_ok(pod_config_blob_encode(POD_POS_S3, blob, sizeof(blob)));
	blob[3] = POD_POS_G; /* tamper: position without updating the CRC */
	zassert_ok(pod_position_resolve(blob, sizeof(blob), POD_POS_S1, &pos, &src));
	zassert_equal(pos, POD_POS_S1, "corrupted block must not select a position");
	zassert_equal(src, POD_POS_SRC_KCONFIG);
}

ZTEST(pod_position, test_bad_magic_and_version_rejected)
{
	uint8_t blob[POD_CFG_BLOB_LEN];
	enum pod_position pos;

	zassert_ok(pod_config_blob_encode(POD_POS_S2, blob, sizeof(blob)));
	blob[0] = 'X';
	blob[4] = pod_config_crc8(blob, 4);
	zassert_ok(pod_position_resolve(blob, sizeof(blob), POD_POS_S1, &pos, NULL));
	zassert_equal(pos, POD_POS_S1);

	zassert_ok(pod_config_blob_encode(POD_POS_S2, blob, sizeof(blob)));
	blob[2] = POD_CFG_VERSION + 1;
	blob[4] = pod_config_crc8(blob, 4);
	zassert_ok(pod_position_resolve(blob, sizeof(blob), POD_POS_S1, &pos, NULL));
	zassert_equal(pos, POD_POS_S1);
}

ZTEST(pod_position, test_out_of_range_position_rejected)
{
	uint8_t blob[POD_CFG_BLOB_LEN] = {POD_CFG_MAGIC0, POD_CFG_MAGIC1, POD_CFG_VERSION, 9, 0};
	enum pod_position pos;

	blob[4] = pod_config_crc8(blob, 4); /* well-formed but impossible position */
	zassert_ok(pod_position_resolve(blob, sizeof(blob), POD_POS_U, &pos, NULL));
	zassert_equal(pos, POD_POS_U);
}

ZTEST(pod_position, test_short_blob_ignored)
{
	uint8_t blob[POD_CFG_BLOB_LEN];
	enum pod_position pos;

	zassert_ok(pod_config_blob_encode(POD_POS_C, blob, sizeof(blob)));
	zassert_ok(pod_position_resolve(blob, 3, POD_POS_S1, &pos, NULL));
	zassert_equal(pos, POD_POS_S1);
}

ZTEST(pod_position, test_invalid_default_is_error)
{
	enum pod_position pos;

	zassert_equal(pod_position_resolve(NULL, 0, POD_POS_COUNT, &pos, NULL), -EINVAL);
	zassert_equal(pod_config_blob_encode(POD_POS_COUNT, (uint8_t[5]){0}, 5), -EINVAL);
}

ZTEST(pod_position, test_roundtrip_every_position)
{
	for (int p = 0; p < POD_POS_COUNT; p++) {
		uint8_t blob[POD_CFG_BLOB_LEN];
		enum pod_position pos;

		zassert_ok(pod_config_blob_encode((enum pod_position)p, blob, sizeof(blob)));
		zassert_ok(pod_position_resolve(blob, sizeof(blob), POD_POS_S1, &pos, NULL));
		zassert_equal(pos, (enum pod_position)p, "%s", pod_position_name(p));
	}
	zassert_str_equal(pod_position_name(POD_POS_S1), "S1");
	zassert_str_equal(pod_position_name(POD_POS_C), "C");
	zassert_equal(POD_POS_COUNT, 6, "blueprint §4.1: six positions");
}
