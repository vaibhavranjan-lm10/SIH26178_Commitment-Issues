"""Integration of the cycle without a board or a real model: loopback RPC,
a stub inferencer with a controllable probability, in-memory SQLite."""
import numpy as np
import pytest

from prahari_hn import mesh, rpc, wire
from prahari_hn.config import Config, NeighbourConfig, PodConfig, UplinkConfig
from prahari_hn.model_registry import ModelBundle, Threshold
from prahari_hn.node import HeadNode
from prahari_hn.store import HOUR, Store
from prahari_hn.uplink import Transport, Uplink

OUTPUTS = ["FL_t0", "FL_t24", "UF_t0", "UF_t6", "FI_t0", "FI_t24", "PO_t0", "PO_t24", "LS_t0", "LS_t24", "HW_t72", "CY_t48", "GL_t0", "WQ_t0", "WQ_t24"]
HEADS = [{"code": c, "name": c, "horizons_h": [int(o.split("_t")[1]) for o in OUTPUTS if o.startswith(c + "_")],
          "graph": c in ("FL", "UF", "FI", "PO"), "trained": c not in ("GL", "WQ")} for c in ("FL", "UF", "FI", "PO", "LS", "HW", "CY", "GL", "WQ")]
CHANNELS = ["P1", "P2", "P3", "P10", "P11", "P12", "P13", "P15", "P17", "P18", "P20", "P22", "P23", "P32", "P33", "P47", "P49", "P51",
            "P52", "P54", "P57", "P58", "P60", "P70", "P75", "P77_lat", "P77_lon", "S1", "S2", "S4", "S6", "S9", "S13", "S15", "S16",
            "S18", "S30", "S33", "S35", "T6", "T11"]


def bundle():
    thr = {o: Threshold(o, 0.3, 0.7, "val_pr_curve") for o in OUTPUTS}
    return ModelBundle("vtest", None, "deadbeef", CHANNELS, [0.0] * 41, [1.0] * 41, OUTPUTS, HEADS, {}, thr,
                       512, 41, 16, 16.0, 3, 8, None, None, "stub", {})


class StubInferencer:
    """Records what it was fed; probability for FL_t0 is settable."""

    def __init__(self):
        self.p_fl = 0.05
        self.calls = []

    def encode(self, x, mask):
        assert x.shape == (512, 41) and mask.shape == (512, 41) and np.isfinite(x).all()
        assert (x[mask == 0] == 0).all()                             # masked → 0, never a value
        self.calls.append(("encode", float(mask.mean())))
        emb = np.linspace(-1, 1, 16, dtype=np.float32) * x[-1, 6] / 30.0
        probs = np.full(15, 0.05, np.float32); probs[0] = self.p_fl
        return emb, mesh.quantise(emb, 16.0), np.zeros(15, np.float32), probs

    def refine(self, own, hist, adj, valid):
        assert hist.shape == (1, 9, 16, 3) and valid[0, 0] == 1
        self.calls.append(("refine", int(valid.sum())))
        probs = np.full(15, 0.05, np.float32); probs[0] = self.p_fl
        return own, np.zeros(15, np.float32), probs


class FakeTransport(Transport):
    def __init__(self):
        self.up, self.posted, self.context = True, [], None

    def post(self, path, payload):
        if self.up:
            self.posted.append((path, payload))
        return self.up

    def get(self, path):
        return self.context if self.up else None


def make_node(tmp_path, clock):
    cfg = Config(node_id="node-A", rpc_device="loopback", db_path=str(tmp_path / "ring.sqlite"), retention_days=60,
                 elevated_cooldown_cycles=2, alert_clear_cycles=1, round_window_s=900,
                 static={"P70": 420.0, "P71": 40.0, "P73": 25.0, "P74": 1.35, "P75": 40, "P77_lat": 23.5, "P77_lon": 85.2,
                         "S30": 5.0, "S33": 120.0, "x_m": 0.0, "y_m": 0.0},
                 pods=[PodConfig(1, "S1"), PodConfig(4, "G"), PodConfig(5, "U")],
                 neighbours=[NeighbourConfig("node-B", 3000.0, 0.0)], uplink=UplinkConfig(endpoint="http://x", transport="http"))
    link = rpc.LoopbackLink()
    store = Store(cfg.db_path, cfg.retention_days)
    tr = FakeTransport()
    up = Uplink(cfg.uplink, cfg.node_id, store, transport=tr)
    inf = StubInferencer()
    node = HeadNode(cfg, link, store=store, bundle=bundle(), inferencer=inf, uplink=up, clock=clock)
    return node, link, store, tr, inf


def pod_line(addr, pos, chans, cycle=1):
    r = wire.Report(pos, wire.RF_RAIL_VALID, cycle, 3300, 0, 0, 0,
                    primary=[wire.Channel(pid, wire.F_VALID, int(v * 1000)) for pid, v in chans.items()])
    return f"$POD {addr} {wire.encode_report(r).hex()}\n"


def feed_history(node, link, t_start, hours):
    for h in range(hours):
        ts = t_start + h * HOUR
        node.clock_now = ts
        link.inject(pod_line(1, 0, {1: 0.25, 2: 0.28, 3: 0.3}, h), pod_line(4, 3, {10: 0.27, 12: 0.5 * h, 45: 6.0}, h),
                    pod_line(5, 4, {13: 28.0 + (h % 24) / 4, 15: 60.0, 17: 1005.0, 18: 2.0, 19: 200.0, 22: 40.0, 23: 70.0}, h))
        for line in link.poll():
            node.handle_line(line)


def test_full_cycle_masking_alerts_duty_uplink_and_recovery(tmp_path):
    t = {"now": 1_757_400_000 // HOUR * HOUR}
    clock = lambda: t["now"]  # noqa: E731
    node, link, store, tr, inf = make_node(tmp_path, clock)
    node.start()
    assert link.sent[:2] == ["$PODS 1,4,5\n", "$CADENCE 3600\n"]
    # 40 days of hourly sweeps (fast: no SWEEP lines yet, so no cycles)
    for h in range(40 * 24):
        t["now"] += HOUR
        ts = t["now"]
        for line in (pod_line(1, 0, {1: 0.25, 2: 0.28, 3: 0.3}, h), pod_line(4, 3, {10: 0.27, 12: 0.5 * h}, h),
                     pod_line(5, 4, {13: 28.0, 15: 60.0, 17: 1005.0, 18: 2.0, 19: 200.0, 22: 40.0, 23: 70.0}, h)):
            node.handle_line(line)
    # the C pod (P14) never exists here → S18 masked; wind present → S15/S16 unmasked
    link.sent.clear()
    r = node.run_cycle(t["now"])
    assert r["refined"] is False and r["n_nbrs"] == 0 and r["alerts"] == []
    x, mask, cov = node.build_window(t["now"])
    ch = node.bundle.channels
    assert mask[:, ch.index("S18")].sum() == 0 and mask[-1, ch.index("S15")] == 1 and mask[-1, ch.index("S2")] == 1
    assert mask[-1, ch.index("T6")] == 1 and mask[-1, ch.index("P70")] == 1 and mask[:, ch.index("P54")].sum() == 0
    assert any(s.startswith("$EMB ") for s in link.sent) and "$STATUS\n" in link.sent
    assert tr.posted[-1][0] == "status" and tr.posted[-1][1]["weight_hash"] == "deadbeef"
    assert store.embedding_history(node.node_id16, 3, t["now"])

    # a neighbour reports this round → graph stage runs; a stale one is dropped
    nb, nc = mesh.fnv1a16("node-B"), mesh.fnv1a16("node-C")
    for k in range(3):
        node.handle_line("$NBR " + mesh.encode_packet(nb, np.full(16, k, np.int8)).hex() + "\n")
        t["now"] += 60
    store.put_embedding(t["now"] - 5000, nc, np.zeros(16, np.int8), own=False)
    store.put_embedding(t["now"] - 4000, nc, np.zeros(16, np.int8), own=False)
    store.put_embedding(t["now"] - 3000, nc, np.zeros(16, np.int8), own=False)
    r = node.run_cycle(t["now"])
    assert r["refined"] is True and r["n_nbrs"] == 1 and inf.calls[-1] == ("refine", 2)

    # warning: siren, CAP queued, uplink up → sent, duty cycle → 5 min
    inf.p_fl = 0.9
    link.sent.clear()
    r = node.run_cycle(t["now"] + HOUR)
    assert r["levels"]["FL"] == 2 and "$ALERT 2 600\n" in link.sent and "$CADENCE 300\n" in link.sent
    assert r["cadence_s"] == 300 and r["sent"] == 1 and tr.posted[-2][0] == "alerts"
    assert tr.posted[-2][1]["cap"]["info"]["event"] == "Riverine flood"
    assert store.unsent_alerts() == []

    # uplink down: alerts queue; forecasting continues; restoration flushes
    tr.up = False
    inf.p_fl = 0.05
    r1 = node.run_cycle(t["now"] + 2 * HOUR)             # clears (clear_cycles=1) → Cancel CAP queued
    assert r1["levels"]["FL"] == 0 and len(store.unsent_alerts()) == 1 and "$ALERT 0\n" in link.sent
    assert r1["cadence_s"] == 300                          # cooldown 2: still elevated after one calm cycle
    r3 = node.run_cycle(t["now"] + 3 * HOUR)
    assert r3["cadence_s"] == 3600 and "$CADENCE 3600\n" in link.sent
    node.run_cycle(t["now"] + 4 * HOUR)
    tr.up = True
    r4 = node.run_cycle(t["now"] + 5 * HOUR)
    assert r4["sent"] == 1 and store.unsent_alerts() == []

    # "reboot": a new HeadNode on the same DB resumes state and still has its window
    inf.p_fl = 0.9
    node.run_cycle(t["now"] + 6 * HOUR)
    assert node.duty.mode == "elevated"
    node2, link2, store2, tr2, inf2 = make_node(tmp_path, clock)
    assert node2.duty.mode == "elevated" and node2.alerts.levels()["FL"] == 2
    x2, mask2, cov2 = node2.build_window(t["now"])          # hour of the last pod reading; later hours have no readings → masked
    assert mask2[-1, ch.index("S2")] == 1 and cov2 > 0.5


def test_bad_lines_and_packets_are_ignored(tmp_path):
    node, link, store, tr, inf = make_node(tmp_path, lambda: 1_757_400_000)
    node.handle_line("garbage\n"); node.handle_line("$POD 1 zz\n"); node.handle_line("$POD 1 0102\n")
    node.handle_line("$NBR " + b"PR".hex() + "\n")
    node.handle_line("$MISSING 4\n")
    assert store.latest_reading_ts() is None and node.pods_present == {4: False}
    node.handle_line("$TIME 1757500000000 locked\n")
    assert node.now() == 1757500000
