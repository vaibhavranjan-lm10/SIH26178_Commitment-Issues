/* SX1262 via Zephyr's native LoRa driver.  Async RX into a message queue
 * so the TDMA loop never blocks in the radio.
 * SPDX-License-Identifier: Apache-2.0 */
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/lora.h>
#include <zephyr/kernel.h>
#include <hn/hw.h>

#define LORA_NODE DT_CHOSEN(prahari_lora)
static const struct device *const radio = DEVICE_DT_GET(LORA_NODE);

struct rx_pkt {
	uint8_t len;
	int16_t rssi;
	uint8_t data[255];
};
K_MSGQ_DEFINE(rx_q, sizeof(struct rx_pkt), 4, 4);

static struct lora_modem_config phy = {
	.bandwidth = BW_125_KHZ,
	.coding_rate = CR_4_5,
	.preamble_len = 8,
	.tx = false,
};
static bool rx_on;

static void rx_cb(const struct device *dev, uint8_t *data, uint16_t size, int16_t rssi,
		  int8_t snr, void *ud)
{
	struct rx_pkt p;

	ARG_UNUSED(dev);
	ARG_UNUSED(snr);
	ARG_UNUSED(ud);
	if (size == 0 || size > sizeof(p.data)) {
		return;
	}
	p.len = (uint8_t)size;
	p.rssi = rssi;
	memcpy(p.data, data, size);
	(void)k_msgq_put(&rx_q, &p, K_NO_WAIT);
}

int hw_lora_init(uint8_t sf, uint32_t freq_hz, int8_t tx_dbm)
{
	if (!device_is_ready(radio)) {
		return -ENODEV;
	}
	phy.frequency = freq_hz;
	phy.datarate = sf;
	phy.tx_power = tx_dbm;
	phy.tx = false;
	return lora_config(radio, &phy);
}

int hw_lora_rx_start(void)
{
	int rc;

	if (rx_on) {
		return 0;
	}
	phy.tx = false;
	rc = lora_config(radio, &phy);
	if (rc) {
		return rc;
	}
	rc = lora_recv_async(radio, rx_cb, NULL);
	rx_on = rc == 0;
	return rc;
}

int hw_lora_rx_stop(void)
{
	if (!rx_on) {
		return 0;
	}
	rx_on = false;
	return lora_recv_async(radio, NULL, NULL);
}

int hw_lora_send(const uint8_t *buf, size_t len)
{
	int rc;

	(void)hw_lora_rx_stop();
	phy.tx = true;
	rc = lora_config(radio, &phy);
	if (rc) {
		return rc;
	}
	rc = lora_send(radio, (uint8_t *)buf, (uint32_t)len);
	phy.tx = false;
	(void)lora_config(radio, &phy);
	return rc;
}

int hw_lora_rx_pop(uint8_t *buf, size_t len, int16_t *rssi)
{
	struct rx_pkt p;

	if (k_msgq_get(&rx_q, &p, K_NO_WAIT)) {
		return 0;
	}
	if (p.len > len) {
		return -ENOSPC;
	}
	memcpy(buf, p.data, p.len);
	if (rssi) {
		*rssi = p.rssi;
	}
	return p.len;
}
