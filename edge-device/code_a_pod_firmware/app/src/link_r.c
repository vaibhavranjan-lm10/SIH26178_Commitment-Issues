/*
 * Mode R — LoRa SX1262, this pod reports on a scheduled slot (blueprint
 * §4.4).  The head node's beacon marks each superframe; the pod opens a
 * receive window for it, re-syncs, then transmits its report in its own
 * slot.  Without a beacon it free-runs (holdover) and keeps reporting.
 *
 * Radio device from the 'prahari,lora' chosen node (a semtech,sx1262 on
 * SPI); PHY from Kconfig.  The Zephyr LoRa API blocks in lora_send /
 * lora_recv, so the beacon window is kept short.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <errno.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/lora.h>
#include <zephyr/sys/printk.h>
#include <prahari/link.h>
#include <prahari/lora_slot.h>
#include <prahari/wire.h>

#define LORA_NODE DT_CHOSEN(prahari_lora)
BUILD_ASSERT(DT_NODE_HAS_STATUS_OKAY(LORA_NODE),
	     "Mode R needs an enabled semtech,sx1262 node named by chosen prahari,lora");

static const struct device *const radio = DEVICE_DT_GET(LORA_NODE);
static struct slot_sched sched;
static uint8_t addr;
static uint8_t position;
static uint8_t report_buf[WIRE_REPORT_MAX];
static uint8_t report_len;
static uint32_t beacon_due, tx_due;
static bool beacon_pending, tx_pending;
static uint32_t beacons, sends;

static struct lora_modem_config phy = {
	.frequency = CONFIG_PRAHARI_LORA_FREQ_HZ,
	.bandwidth = BW_125_KHZ,
	.datarate = CONFIG_PRAHARI_LORA_SF,
	.coding_rate = CR_4_5,
	.preamble_len = 8,
	.tx_power = CONFIG_PRAHARI_LORA_TX_DBM,
	.tx = false,
};

static int radio_mode(bool tx)
{
	phy.tx = tx;
	return lora_config(radio, &phy);
}

static void reschedule(uint32_t now)
{
	tx_pending = slot_sched_next_tx(&sched, now, &tx_due) == 0;
	beacon_pending = slot_sched_next_beacon(&sched, now, &beacon_due) == 0;
}

int link_init(const struct link_identity *id, link_arm_cb_t arm_cb, void *ctx)
{
	int rc;
	uint32_t now = k_uptime_get_32();

	ARG_UNUSED(arm_cb); /* downlink commands ride on the beacon in a later step */
	ARG_UNUSED(ctx);
	if (!device_is_ready(radio)) {
		return -ENODEV;
	}
	rc = slot_sched_init(&sched, CONFIG_PRAHARI_SLOT_SUPERFRAME_MS, CONFIG_PRAHARI_SLOT_MS,
			     CONFIG_PRAHARI_SLOT_BEACON_MS, CONFIG_PRAHARI_SLOT_N, id->slot,
			     CONFIG_PRAHARI_SLOT_GUARD_MS, CONFIG_PRAHARI_SLOT_HOLDOVER_MS);
	if (rc) {
		return rc;
	}
	addr = id->addr;
	position = id->position;
	rc = radio_mode(false);
	if (rc) {
		return rc;
	}
	slot_sched_start_free_running(&sched, now);
	reschedule(now);
	return 0;
}

int link_publish(const struct pod_report *r)
{
	int n = wire_encode_report(r, position, report_buf, sizeof(report_buf));

	if (n < 0) {
		return n;
	}
	report_len = (uint8_t)n;
	return 0;
}

static void beacon_window(uint32_t now)
{
	uint8_t buf[WIRE_BEACON_LEN];
	struct wire_beacon b;
	int16_t rssi;
	int8_t snr;
	int n;

	if (radio_mode(false)) {
		return;
	}
	n = lora_recv(radio, buf, sizeof(buf),
		      K_MSEC(CONFIG_PRAHARI_SLOT_BEACON_MS + 2 * CONFIG_PRAHARI_SLOT_GUARD_MS),
		      &rssi, &snr);
	if (n == WIRE_BEACON_LEN && wire_decode_beacon(buf, (size_t)n, &b) == 0) {
		uint32_t rx = k_uptime_get_32();
		uint32_t air = lora_time_on_air_ms(WIRE_BEACON_LEN, CONFIG_PRAHARI_LORA_SF, 125000,
						   5, 8, true, true, false);

		slot_sched_sync(&sched, rx, (uint16_t)air);
		beacons++;
	}
	ARG_UNUSED(now);
}

static void slot_transmit(void)
{
	uint8_t pkt[WIRE_REPORT_MAX + 5];
	int n;

	if (report_len == 0) {
		return; /* nothing processed yet: the slot stays empty */
	}
	n = wire_encode_lora_uplink(addr, report_buf, report_len, pkt, sizeof(pkt));
	if (n < 0 || radio_mode(true)) {
		return;
	}
	if (lora_send(radio, pkt, (uint32_t)n) == 0) {
		sends++;
	}
	(void)radio_mode(false);
}

void link_service(uint32_t now_ms)
{
	(void)slot_sched_update(&sched, now_ms);

	if (beacon_pending && (int32_t)(now_ms - (beacon_due - CONFIG_PRAHARI_SLOT_GUARD_MS)) >= 0) {
		beacon_window(now_ms);
		reschedule(k_uptime_get_32() + 1);
		return;
	}
	if (tx_pending && (int32_t)(now_ms - tx_due) >= 0) {
		slot_transmit();
		reschedule(k_uptime_get_32() + 1);
	}
}

void link_wait(k_timeout_t timeout)
{
	uint32_t now = k_uptime_get_32();
	uint32_t next = UINT32_MAX;
	int32_t d;

	/* Sleep no longer than the next scheduled event. */
	if (beacon_pending) {
		next = beacon_due - CONFIG_PRAHARI_SLOT_GUARD_MS;
	}
	if (tx_pending && (int32_t)(tx_due - next) < 0) {
		next = tx_due;
	}
	d = (int32_t)(next - now);
	if (next != UINT32_MAX && d >= 0 && (uint32_t)d < k_ticks_to_ms_floor32(timeout.ticks)) {
		timeout = K_MSEC(d);
	}
	k_sleep(timeout);
}

const char *link_mode_name(void)
{
	return sched.synced ? "R/LoRa synced" : "R/LoRa holdover";
}
