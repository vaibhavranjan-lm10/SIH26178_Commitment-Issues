/*
 * PRAHARI Code B — head node MCU side (blueprint §6.1).
 *
 * One 10 ms supervisory loop drives:
 *   rails      power sequencing of the node's own rails, load shedding
 *   bus        RS-485 master: one sweep per cadence, silent pods marked
 *   tdma       LoRa: pod beacon + pod uplink window, mesh slot from the
 *              node-ID hash on PPS-disciplined UTC (holdover on local time)
 *   pps/gnss   UTC-of-second + pulse capture
 *   alert      siren actuator, commanded by the Linux side
 *   bridge     MsgPack-RPC byte channel to the Linux side — see
 *              hn/bridge_contract.h and code_b_head_node/README.md for
 *              the actual message contract this replaced the earlier
 *              ad hoc `$`-line protocol with
 *   supervisor feeds the IWDG only while all of the above check in
 *
 * No hazard decision is made here.  The Linux side owns the model, the
 * thresholds and the alert decision; this side senses, times and acts.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>
#include <zephyr/kernel.h>
#include <prahari/lora_slot.h> /* lora_time_on_air_ms (shared protocol helper) */
#include <prahari/wire.h>
#include <hn/alert.h>
#include <hn/bridge_contract.h>
#include <hn/bus_master.h>
#include <hn/hw.h>
#include <hn/mprpc.h>
#include <hn/pps.h>
#include <hn/rails.h>
#include <hn/supervisor.h>
#include <hn/tdma.h>

/* ---- rails (§6.4) ---- */
enum { RAIL_12V_MAST = 0, RAIL_3V3_RADIO };
static const struct rail_cfg rail_cfg[] = {
	[RAIL_12V_MAST] = {"12V mast bus", 200, 2, CONFIG_HN_BATT_SHED_MV, CONFIG_HN_BATT_RESTORE_MV},
	[RAIL_3V3_RADIO] = {"3V3 radios", 50, 1, CONFIG_HN_BATT_SHED_MV - 400,
			    CONFIG_HN_BATT_RESTORE_MV - 400},
};
static struct rails rails;

/* ---- bus ---- */
static struct bus_master bus;
static uint32_t cadence_ms = CONFIG_HN_SWEEP_CADENCE_S * 1000u;
static uint32_t t_last_sweep;

/* ---- tdma ---- */
static const struct tdma_cfg tdma = {
	.superframe_ms = CONFIG_HN_TDMA_SUPERFRAME_MS,
	.beacon_ms = CONFIG_HN_TDMA_BEACON_MS,
	.pod_slot_ms = CONFIG_HN_TDMA_POD_SLOT_MS,
	.n_pod_slots = CONFIG_HN_TDMA_POD_SLOTS,
	.mesh_slot_ms = CONFIG_HN_TDMA_MESH_SLOT_MS,
	.n_mesh_slots = CONFIG_HN_TDMA_MESH_SLOTS,
	.guard_ms = CONFIG_HN_TDMA_GUARD_MS,
};
static struct tdma_layout layout;
static uint8_t mesh_slot;
static uint64_t next_beacon, next_mesh_tx, rx_stop_at;
static bool lora_ok, rx_window;
static uint8_t emb[BC_MAX_EMB];
static uint8_t emb_len;

/* ---- time ---- */
static struct pps_disc pps;

/* ---- alert, bridge, supervisor ---- */
static struct alert_out alert;
static struct supervisor sup;
static int ch_main, ch_bus, ch_tdma, ch_rpc;

/* Inbound byte accumulator for the streaming MessagePack-RPC decoder —
 * bytes trickle in from the UART ISR ring buffer; mp_scan_value finds the
 * boundary of each complete message as they arrive. */
#define RX_ACC_MAX 512
static uint8_t rx_acc[RX_ACC_MAX];
static size_t rx_acc_len;

static uint64_t utc_now(bool *locked)
{
	uint64_t utc;

	if (pps_local_to_utc_ms(&pps, hw_now_us(), &utc) == 0) {
		*locked = pps.state == PPS_LOCKED;
		return utc;
	}
	*locked = false;
	return k_uptime_get(); /* free-running until GNSS time arrives */
}

static uint8_t parse_addr_list(const char *s, uint8_t *out, uint8_t max)
{
	uint8_t n = 0;
	unsigned int v = 0;
	bool in = false;

	for (; ; s++) {
		if (*s >= '0' && *s <= '9') {
			v = v * 10 + (unsigned)(*s - '0');
			in = true;
		} else {
			if (in && n < max && v <= RS485_ADDR_MAX && rs485_addr_valid((uint8_t)v)) {
				out[n++] = (uint8_t)v;
			}
			v = 0;
			in = false;
			if (*s == '\0') {
				break;
			}
		}
	}
	return n;
}

static void send_msg(int n, uint8_t *buf)
{
	if (n > 0) {
		hw_rpc_send_bytes(buf, (size_t)n);
	}
}

static void emit_pod_report(uint8_t addr, const uint8_t *rep, size_t len)
{
	uint8_t buf[16 + WIRE_REPORT_MAX];

	send_msg(bc_encode_pod_report(buf, sizeof(buf), addr, rep, len), buf);
}

static void fill_status(struct bc_status *st)
{
	bool locked;

	utc_now(&locked);
	st->sweeps = bus.sweeps;
	st->pods = bus.n_pods;
	strncpy(st->pps, pps.state == PPS_LOCKED ? "locked" : pps.state == PPS_HOLDOVER ? "holdover"
											 : "unlocked",
	       BC_MAX_STR);
	st->pps[BC_MAX_STR] = '\0';
	st->drift_ppm = pps_drift_ppm(&pps);
	st->batt_mv = hw_batt_mv();
}

static void handle_bridge_msg(const struct bc_msg *m, uint32_t now)
{
	uint8_t buf[128];

	switch (m->type) {
	case BC_MSG_SET_ALERT:
		(void)alert_command(&alert, (enum alert_level)m->u.set_alert.level,
				   m->u.set_alert.seconds * 1000u, now);
		break;
	case BC_MSG_SET_PODS:
		if (!bus_sweep_active(&bus)) {
			(void)bus_master_init(&bus, m->u.set_pods.addrs, m->u.set_pods.n_addrs,
					      CONFIG_HN_BUS_REPLY_TIMEOUT_MS, CONFIG_HN_BUS_TURNAROUND_MS,
					      hw_bus_send, NULL);
		}
		break;
	case BC_MSG_SET_CADENCE:
		cadence_ms = m->u.set_cadence.seconds * 1000u; /* risk-adaptive duty cycle, §6.4 */
		break;
	case BC_MSG_SET_ARMED:
		(void)bus_send_arm(&bus, RS485_ADDR_BROADCAST, m->u.set_armed.armed);
		break;
	case BC_MSG_SET_EMBEDDING:
		memcpy(emb, m->u.set_embedding.emb, m->u.set_embedding.emb_len);
		emb_len = m->u.set_embedding.emb_len;
		break;
	case BC_MSG_GET_STATUS_CALL: {
		struct bc_status st;

		fill_status(&st);
		send_msg(bc_encode_get_status_response(buf, sizeof(buf), m->msg_id, &st), buf);
		break;
	}
	default:
		break; /* BC_MSG_NONE, or a message this side never expects to receive */
	}
}

static void poll_bridge(uint32_t now)
{
	for (;;) {
		size_t got = hw_rpc_read(rx_acc + rx_acc_len, RX_ACC_MAX - rx_acc_len);

		rx_acc_len += got;
		if (got == 0) {
			break;
		}
	}
	for (;;) {
		size_t msg_len;
		enum mp_scan_result r = mp_scan_value(rx_acc, rx_acc_len, &msg_len);

		if (r == MP_SCAN_NEED_MORE) {
			break;
		}
		if (r == MP_SCAN_ERROR) {
			/* Resync: drop one byte and keep scanning — a single corrupted
			 * byte must not wedge the channel for the rest of the boot. */
			memmove(rx_acc, rx_acc + 1, --rx_acc_len);
			continue;
		}
		struct bc_msg m;

		if (bc_decode(rx_acc, msg_len, &m) == 0) {
			handle_bridge_msg(&m, now);
			sup_checkin(&sup, ch_rpc, now);
		}
		memmove(rx_acc, rx_acc + msg_len, rx_acc_len - msg_len);
		rx_acc_len -= msg_len;
	}
}

static void service_bus(uint32_t now)
{
	struct rs485_frame f;
	uint8_t idx;
	enum bus_event ev;
	uint8_t buf[32];

	hw_bus_idle(now);
	while (hw_bus_rx_frame(&f, now)) {
		ev = bus_rx_frame(&bus, &f, now, &idx);
		if (ev == BUS_EVT_REPORT) {
			emit_pod_report(bus.pods[idx].addr, bus.pods[idx].report,
					bus.pods[idx].report_len);
		}
	}
	ev = bus_tick(&bus, now, &idx);
	if (ev == BUS_EVT_MISSING) {
		send_msg(bc_encode_pod_missing(buf, sizeof(buf), bus.pods[idx].addr), buf);
	} else if (ev == BUS_EVT_SWEEP_DONE) {
		uint8_t present = 0;
		struct bc_status st;

		for (uint8_t i = 0; i < bus.n_pods; i++) {
			present += bus.pods[i].present;
		}
		send_msg(bc_encode_sweep_done(buf, sizeof(buf), bus.sweeps, present, bus.n_pods), buf);
		/* Pod health rides along every sweep, not only on request. */
		fill_status(&st);
		{
			uint8_t sbuf[96];

			send_msg(bc_encode_node_status(sbuf, sizeof(sbuf), &st), sbuf);
		}
		sup_checkin(&sup, ch_bus, now);
	}
	if (!bus_sweep_active(&bus) && (uint32_t)(now - t_last_sweep) >= cadence_ms) {
		t_last_sweep = now;
		if (bus_sweep_start(&bus, now) == -ENODEV) {
			sup_checkin(&sup, ch_bus, now); /* no pods configured: not a hang */
		}
	}
}

static void service_tdma(uint32_t now)
{
	bool locked;
	uint64_t utc = utc_now(&locked);
	uint8_t pkt[256];
	int n;

	if (!lora_ok) {
		sup_checkin(&sup, ch_tdma, now);
		return;
	}

	/* Superframe start: beacon to our Mode R pods, then listen for them. */
	if (utc >= next_beacon) {
		struct wire_beacon b = {
			.superframe_idx = (uint16_t)tdma_superframe_index(&tdma, utc),
			.slot_ms = tdma.pod_slot_ms,
			.n_slots = tdma.n_pod_slots,
			.beacon_ms = tdma.beacon_ms,
		};

		n = wire_encode_beacon(&b, pkt, sizeof(pkt));
		if (n > 0) {
			(void)hw_lora_send(pkt, (size_t)n);
		}
		(void)hw_lora_rx_start();
		rx_window = true;
		rx_stop_at = next_beacon + layout.used_ms; /* pod region then mesh region */
		next_beacon = tdma_next_beacon(&tdma, utc + 1);
		sup_checkin(&sup, ch_tdma, now);
		/* GNSS-disciplined time, pushed once per superframe. */
		uint8_t tbuf[32];

		send_msg(bc_encode_time_sync(tbuf, sizeof(tbuf), utc, locked ? "locked" : "holdover"), tbuf);
	}

	/* Our mesh slot: broadcast the embedding the Linux side gave us (§7.4 step 4). */
	if (utc >= next_mesh_tx) {
		if (emb_len) {
			(void)hw_lora_send(emb, emb_len);
			if (rx_window) {
				(void)hw_lora_rx_start();
			}
		}
		next_mesh_tx = tdma_next_mesh_tx(&tdma, utc + 1, mesh_slot);
	}

	if (rx_window && utc >= rx_stop_at) {
		(void)hw_lora_rx_stop();
		rx_window = false;
	}

	/* Anything received: a pod uplink (decoded here) or a neighbour's packet. */
	while ((n = hw_lora_rx_pop(pkt, sizeof(pkt), NULL)) > 0) {
		uint8_t addr;
		const uint8_t *rep;
		size_t rlen;

		if (wire_decode_lora_uplink(pkt, (size_t)n, &addr, &rep, &rlen) == 0) {
			emit_pod_report(addr, rep, rlen);
		} else {
			uint8_t nbuf[16 + 256];

			send_msg(bc_encode_neighbour_packet(nbuf, sizeof(nbuf), pkt, (size_t)n), nbuf);
		}
	}
}

int main(void)
{
	uint32_t now = k_uptime_get_32();
	uint8_t addrs[BUS_MAX_PODS];
	uint8_t n_addrs;
	const char *node_id = CONFIG_HN_NODE_ID;
	int rc;

	sup_init(&sup, now);
	ch_main = sup_register(&sup, "main", 2000, true);
	ch_bus = sup_register(&sup, "bus", 3 * cadence_ms + 20000, true);
	ch_tdma = sup_register(&sup, "tdma", 3 * tdma.superframe_ms, true);
	ch_rpc = sup_register(&sup, "rpc", 0xFFFFFFFFu, false); /* informational */
	(void)hw_wdt_init(CONFIG_HN_WDT_TIMEOUT_MS);

	/* 1. Rails up in order: mast bus (pods need time to boot), then radios. */
	rc = hw_rails_init();
	(void)rc;
	(void)rails_init(&rails, rail_cfg, ARRAY_SIZE(rail_cfg), hw_rail_set, NULL, now);
	rails_request(&rails, RAIL_12V_MAST, true);
	rails_request(&rails, RAIL_3V3_RADIO, true);
	while (!rails_all_settled(&rails)) {
		rails_tick(&rails, k_uptime_get_32(), hw_batt_mv() ? hw_batt_mv() : 12800);
		k_sleep(K_MSEC(10));
	}

	/* 2. Links. */
	(void)hw_rpc_init();
	(void)hw_bus_init();
	n_addrs = parse_addr_list(CONFIG_HN_POD_ADDRS, addrs, BUS_MAX_PODS);
	(void)bus_master_init(&bus, addrs, n_addrs, CONFIG_HN_BUS_REPLY_TIMEOUT_MS,
			      CONFIG_HN_BUS_TURNAROUND_MS, hw_bus_send, NULL);

	pps_init(&pps, CONFIG_HN_PPS_TOL_US, CONFIG_HN_PPS_LOCK_PULSES,
		 CONFIG_HN_PPS_HOLDOVER_S * 1000000u);
	(void)hw_gnss_init(&pps);

	(void)tdma_layout(&tdma, &layout);
	mesh_slot = tdma_mesh_slot((const uint8_t *)node_id, strlen(node_id), tdma.n_mesh_slots);
	rc = hw_lora_init(CONFIG_HN_LORA_SF, CONFIG_HN_LORA_FREQ_HZ, CONFIG_HN_LORA_TX_DBM);
	lora_ok = rc == 0;
	{
		bool locked;
		uint64_t utc = utc_now(&locked);

		next_beacon = tdma_next_beacon(&tdma, utc);
		next_mesh_tx = tdma_next_mesh_tx(&tdma, utc, mesh_slot);
	}

	alert_init(&alert, 1000, 2000, CONFIG_HN_ALERT_MAX_S * 1000u);
	(void)hw_alert_init();

	t_last_sweep = k_uptime_get_32() - cadence_ms; /* first sweep immediately */

	while (1) {
		now = k_uptime_get_32();
		hw_clock_service();
		(void)pps_tick(&pps, hw_now_us());

		rails_tick(&rails, now, hw_batt_mv());
		service_bus(now);
		service_tdma(now);
		poll_bridge(now);

		hw_alert_set(alert_tick(&alert, now));

		sup_checkin(&sup, ch_main, now);
		if (sup_should_feed(&sup, now)) {
			hw_wdt_feed();
		}
		k_sleep(K_MSEC(10));
	}
	return 0;
}
