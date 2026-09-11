"""The head node's Linux-side main loop — blueprint §6.7 execution order,
§6.8 alerting, §6.4 duty cycling, glued to the MCU over the RPC bridge.

KNOWN GAP: this module still talks the old ad hoc line protocol via
rpc.py, which mcu/'s firmware no longer speaks (see ../README.md "Known
gap: linux/prahari_hn/node.py is not yet on this bridge"). The real
contract is prahari_hn.bridge.HeadNodeBridge; porting this loop onto it
is separate follow-up work, not done in the same change that introduced
bridge.py.

One inference cycle (``run_cycle``):
  1. window: the last 512 hourly steps of the 41 trained channels from the
     ring buffer (pod readings + satellite context + static site block),
     mask for anything absent — never imputed
  2. encoder.onnx → own 16-d embedding (+ int8 for the mesh) and the
     standalone logits/probs; hand the mesh packet to the MCU ($EMB)
  3. graph stage, only if at least one neighbour's packet arrived within
     this round's window: graph.onnx over the ego-network histories.  A
     late or missing neighbour is dropped for the round; no neighbours →
     the standalone result stands.  Never waits.
  4./5. heads and temperature scaling are inside the graphs; ``probs`` is
     already calibrated
  6. alert decisioning → $ALERT to the MCU siren, CAP queued in SQLite,
     queue flushed if the uplink is up
  7. duty cycle → $CADENCE to the MCU when it changes
  8. status/telemetry uplink (weight hash etc.), context vector poll, prune

Time comes from the MCU's $TIME (GNSS-disciplined) when locked, else the
system clock.  Everything the cycle needs to survive a reboot is in the
ring buffer.
"""
from __future__ import annotations

import logging
import time

import numpy as np

from . import features as F
from . import mesh, rpc
from .alerts import AlertEngine, LEVEL_ADVISORY, LEVEL_NONE, LEVEL_WARNING
from .config import Config
from .duty_cycle import DutyCycle
from .inference import Inferencer
from .model_registry import ModelBundle, load_bundle
from .store import Store
from .uplink import Uplink
from .wire import WireError, decode_report

log = logging.getLogger("prahari_hn")


class HeadNode:
    def __init__(self, cfg: Config, link: rpc.RpcLink, store: Store | None = None,
                 bundle: ModelBundle | None = None, inferencer: Inferencer | None = None,
                 uplink: Uplink | None = None, clock=time.time):
        self.cfg, self.link, self.clock = cfg, link, clock
        self.store = store or Store(cfg.db_path, cfg.retention_days)
        self.bundle = bundle or load_bundle(cfg.models_root, cfg.model_version, cfg.prefer_int8)
        self.infer = inferencer or Inferencer(self.bundle, cfg.ort_threads)
        self.uplink = uplink or Uplink(cfg.uplink, cfg.node_id, self.store)
        self.node_id16 = mesh.fnv1a16(cfg.node_id)
        self.alerts = AlertEngine(self.bundle, cfg.node_id, cfg.alert_clear_cycles, cfg.site_name)
        self.alerts.import_state(self.store.get_state("alert_levels"))
        self.duty = DutyCycle(cfg.baseline_cadence_s, cfg.elevated_cadence_s, cfg.elevated_cooldown_cycles)
        self.duty.import_state(self.store.get_state("duty_cycle"))
        self.nbr_xy = {mesh.fnv1a16(n.node_id): (n.x_m, n.y_m) for n in cfg.neighbours}
        self.own_xy = ((cfg.static["x_m"], cfg.static["y_m"]) if "x_m" in cfg.static and "y_m" in cfg.static else None)
        self.mcu_utc_ms: int | None = None
        self.mcu_utc_at: float | None = None
        self.mcu_time_state = "unlocked"
        self.mcu_stat: dict[str, str] = {}
        self.pods_present: dict[int, bool] = {}
        self.cycles = 0
        self.last_cycle_ts: int | None = None
        self.last_probs: dict[str, float] = {}
        if self.store.integrity != "ok":
            log.error("ring buffer integrity: %s", self.store.integrity)
        log.info("model %s weight %s (%s)", self.bundle.version, self.bundle.weight_hash[:12], self.bundle.training_data_note)

    # ---------------------------------------------------------------- time
    def now(self) -> int:
        if self.mcu_utc_ms is not None and self.mcu_time_state in ("locked", "holdover"):
            return int(self.mcu_utc_ms / 1000 + (self.clock() - self.mcu_utc_at))
        return int(self.clock())

    # ---------------------------------------------------------------- MCU → Linux
    def start(self) -> None:
        """Configure the MCU: poll list, cadence, ask for time/status."""
        if self.cfg.pods:
            self.link.send(rpc.fmt_pods(self.cfg.pod_addrs))
        self.link.send(rpc.fmt_cadence(self.duty.cadence_s))
        self.link.send(rpc.fmt_status())

    def handle_line(self, line: str) -> None:
        try:
            ev = rpc.parse_line(line)
        except rpc.RpcError as e:
            log.warning("rpc: %s", e)
            return
        if ev is None:
            return
        ts = self.now()
        if isinstance(ev, rpc.PodReportEvent):
            self.ingest_report(ev.addr, ev.report, ts)
        elif isinstance(ev, rpc.MissingEvent):
            self.pods_present[ev.addr] = False        # its channels are simply absent this sweep → masked
        elif isinstance(ev, rpc.SweepEvent):
            self.on_sweep(ev, ts)
        elif isinstance(ev, rpc.NeighbourEvent):
            self.ingest_neighbour(ev.packet, ts)
        elif isinstance(ev, rpc.TimeEvent):
            self.mcu_utc_ms, self.mcu_utc_at, self.mcu_time_state = ev.utc_ms, self.clock(), ev.state
        elif isinstance(ev, rpc.StatEvent):
            self.mcu_stat = ev.fields

    def ingest_report(self, addr: int, report: bytes, ts: int) -> None:
        try:
            r = decode_report(report)
        except WireError as e:
            log.warning("pod %d: bad report (%s)", addr, e)
            return
        self.pods_present[addr] = True
        values = {}
        for c in r.primary:
            if c.usable:                                  # FAULT/NOPOWER/RAW/UNCAL → not stored → masked
                values[f"P{c.id}"] = (c.value, c.flags)
        # Pod-side derivations (S13–S16, S21) are stored for cross-checking only;
        # the model's S-channels are recomputed here from the primaries so they
        # match the training reference exactly.
        for c in r.derived:
            if c.usable:
                values[f"pod:S{c.id}"] = (c.value, c.flags)
        if r.flags & 2:
            values[f"pod{addr}:rail_mv"] = (float(r.rail_mv), 0)
        if values:
            self.store.put_readings(ts, values)

    def ingest_neighbour(self, packet: bytes, ts: int) -> None:
        d = mesh.decode_packet(packet)
        if d is None:
            return                                        # not a mesh packet: dropped
        nid, emb = d
        if nid == self.node_id16:
            return
        self.store.put_embedding(ts, nid, emb, own=False)

    def on_sweep(self, ev: rpc.SweepEvent, ts: int) -> None:
        # One sweep per cadence tick (§5.2) → one inference cycle per sweep.
        self.run_cycle(ts)

    # ---------------------------------------------------------------- the cycle
    def build_window(self, ts: int) -> tuple[np.ndarray, np.ndarray, float]:
        L = self.bundle.context_length
        hours = L + F.HISTORY_HOURS
        vals, _ = self.store.hourly_matrix(F.PRIMARY_INPUTS, ts, hours)
        prim = {c: vals[:, i] for i, c in enumerate(F.PRIMARY_INPUTS)}
        x, mask = F.compute_trained_channels(prim, self.cfg.static, self.bundle.channels, L)
        return x, mask, float(mask.mean())

    def run_cycle(self, ts: int | None = None) -> dict:
        ts = int(ts if ts is not None else self.now())
        t0 = time.perf_counter()
        # 1–2. window → encoder (standalone path: always)
        x, mask, coverage = self.build_window(ts)
        emb, emb_q, logits, probs = self.infer.encode(x, mask)
        self.store.put_embedding(ts, self.node_id16, emb_q, own=True)
        self.link.send(rpc.fmt_emb(mesh.encode_packet(self.node_id16, emb_q)))   # for the MCU's next mesh slot
        refined, n_nbrs = False, 0
        # 3. graph stage — only over what arrived this round; never waits
        nbrs = self.collect_neighbours(ts)
        if nbrs:
            own_hist = self.own_history(ts, emb)
            hist, adj, valid = mesh.build_graph_inputs(own_hist, nbrs, self.bundle.max_neighbours)
            try:
                _, logits, probs = self.infer.refine(emb, hist, adj, valid)
                refined, n_nbrs = True, len(nbrs)
            except Exception as e:                        # graph stage is optional; standalone result stands
                log.warning("graph stage failed, using own embedding: %s", e)
        prob_map = {name: float(p) for name, p in zip(self.bundle.outputs, probs)}
        self.last_probs = prob_map
        # 6. alerts
        new_alerts = self.alerts.decide(prob_map, ts)
        for a in new_alerts:
            self.store.queue_alert(ts, a.output, a.level, a.cap)
            log.warning("ALERT %s %s→%s p=%.2f (%s) thr=%.2f [%s]", a.head, a.prev_level, a.level, a.probability,
                        a.output, a.threshold, a.threshold_source)
        self.drive_siren(new_alerts)
        self.store.set_state("alert_levels", self.alerts.export_state())
        # 7. duty cycle
        if self.duty.update(self.alerts.any_warning()):
            self.link.send(rpc.fmt_cadence(self.duty.cadence_s))
            log.info("cadence → %ds (%s)", self.duty.cadence_s, self.duty.mode)
        self.store.set_state("duty_cycle", self.duty.export_state())
        # 8. uplink: flush queue, status, context; then housekeeping
        sent = self.uplink.flush_alerts(ts)
        if self.cycles % max(1, self.cfg.uplink.status_every_cycles) == 0:
            self.uplink.send_status(self.status(ts, prob_map, refined, n_nbrs, coverage))
        self.uplink.poll_context()
        self.store.log_run(ts, refined, n_nbrs, self.bundle.weight_hash, prob_map)
        if self.cycles % 24 == 0:
            self.store.prune(ts)
        self.cycles += 1
        self.last_cycle_ts = ts
        self.link.send(rpc.fmt_status())
        return {"ts": ts, "probs": prob_map, "refined": refined, "n_nbrs": n_nbrs, "coverage": coverage,
                "alerts": new_alerts, "sent": sent, "cadence_s": self.duty.cadence_s,
                "levels": self.alerts.levels(), "elapsed_s": time.perf_counter() - t0}

    def collect_neighbours(self, ts: int) -> list[mesh.NeighbourHistory]:
        P = self.bundle.graph_periods
        out = []
        for nid, last in self.store.neighbours_seen(ts - self.cfg.round_window_s):
            hist = self.store.embedding_history(nid, P, ts)
            if len(hist) < P or last < ts - self.cfg.round_window_s:
                continue                                  # late or too new for the period window: dropped
            arr = np.stack([mesh.dequantise(e, self.bundle.emb_int8_scale) for _, e in hist])
            out.append(mesh.NeighbourHistory(nid, arr, mesh.adjacency_prior(self.own_xy, self.nbr_xy.get(nid)), last))
        return out

    def own_history(self, ts: int, emb_now: np.ndarray) -> np.ndarray:
        P = self.bundle.graph_periods
        hist = self.store.embedding_history(self.node_id16, P, ts)
        arr = [mesh.dequantise(e, self.bundle.emb_int8_scale) for _, e in hist]
        while len(arr) < P:                               # first cycles after boot: repeat the current one
            arr.insert(0, emb_now.astype(np.float32))
        arr[-1] = emb_now.astype(np.float32)
        return np.stack(arr[-P:])

    def drive_siren(self, new_alerts) -> None:
        if not new_alerts:
            return
        top = max(self.alerts.levels().values(), default=LEVEL_NONE)
        if top == LEVEL_WARNING and any(a.level == LEVEL_WARNING for a in new_alerts):
            self.link.send(rpc.fmt_alert(rpc.ALERT_WARNING, self.cfg.siren_warning_s))
        elif top == LEVEL_ADVISORY and any(a.level == LEVEL_ADVISORY and a.escalation for a in new_alerts):
            self.link.send(rpc.fmt_alert(rpc.ALERT_ADVISORY, self.cfg.siren_advisory_s))
        elif top == LEVEL_NONE:
            self.link.send(rpc.fmt_alert(rpc.ALERT_OFF))

    def status(self, ts, probs, refined, n_nbrs, coverage) -> dict:
        return {"ts": ts, "model_version": self.bundle.version, "weight_hash": self.bundle.weight_hash,
                "levels": self.alerts.levels(), "probs": probs, "graph_refined": refined, "n_neighbours": n_nbrs,
                "window_coverage": coverage, "cadence_s": self.duty.cadence_s, "duty_mode": self.duty.mode,
                "pods": {str(a): p for a, p in self.pods_present.items()}, "mcu": self.mcu_stat,
                "time_state": self.mcu_time_state, "unsent_alerts": len(self.store.unsent_alerts())}

    # ---------------------------------------------------------------- loop
    def run_forever(self, poll_s: float = 0.2, fallback_cycle: bool = True) -> None:
        """Service the RPC link; a sweep triggers a cycle.  If the MCU goes quiet
        for two cadences, run a cycle anyway on the stored history."""
        self.start()
        while True:
            for line in self.link.poll():
                self.handle_line(line)
            now = self.now()
            if fallback_cycle and (self.last_cycle_ts is None or now - self.last_cycle_ts > 2 * self.duty.cadence_s):
                self.run_cycle(now)
            time.sleep(poll_s)
