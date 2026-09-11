"""HeadNodeBridge — the Linux-side implementation of the actual MCU<->Linux
message contract, over the UNO Q's real MessagePack-RPC bridge.

Uses the official `arduino_router_bridge.Bridge` (the same package and
protocol version the Arduino-supplied Linux image ships), talking to the
on-board Router at ``unix:///var/run/arduino-router.sock`` in production.
Development/testing uses the officially-documented ``tcp://host:port``
address instead (the package itself refuses ``unix://`` off the real
board) — see config/dev-harness.toml and tests/test_bridge_integration.py.

Message shapes and the full rationale live in
code_b_head_node/README.md ("The MCU <-> Linux message contract"); the
canonical shape reference is code_b_head_node/mcu/include/hn/bridge_contract.h,
which this module's method names and argument order must keep matching —
there is no code generation tying the two together, so a change on one
side that is not made on the other is a silent contract break, not a
build error. The wire bytes are cross-checked against the C side's own
encoder/decoder in tests/test_bridge_integration.py.

Six methods are PROVIDED here (the MCU calls or notifies them; sensor
readings and pod health flow this direction). Five are fire-and-forget
NOTIFY calls this side makes (alert-trigger and duty-cycle-change
commands flow this direction). One, get_status, is a blocking CALL this
side makes when it wants a synchronous read rather than waiting for the
next node_status push.
"""
from __future__ import annotations

import logging
import queue
import time
from dataclasses import dataclass

from arduino.router_bridge import DEFAULT_ADDRESS, Bridge, RpcError

log = logging.getLogger("prahari_hn.bridge")


# ---------------------------------------------------------------- MCU -> Linux events
@dataclass
class PodReport:
    addr: int
    report: bytes
    ts: float


@dataclass
class PodMissing:
    addr: int
    ts: float


@dataclass
class SweepDone:
    sweep: int
    present: int
    total: int
    ts: float


@dataclass
class NeighbourPacket:
    packet: bytes
    ts: float


@dataclass
class TimeSync:
    utc_ms: int
    state: str  # "locked" | "holdover" | "unlocked"
    ts: float


@dataclass
class NodeStatus:
    sweeps: int
    pods: int
    pps: str
    drift_ppm: int
    batt_mv: int
    ts: float


BridgeEvent = PodReport | PodMissing | SweepDone | NeighbourPacket | TimeSync | NodeStatus


def _status_from_map(d: dict) -> dict:
    return {"sweeps": int(d["sweeps"]), "pods": int(d["pods"]), "pps": str(d["pps"]),
            "drift_ppm": int(d["drift_ppm"]), "batt_mv": int(d["batt_mv"])}


class HeadNodeBridge:
    def __init__(self, address: str = DEFAULT_ADDRESS, max_queue: int = 4096):
        self._bridge = Bridge(address)
        self.events: queue.Queue[BridgeEvent] = queue.Queue(maxsize=max_queue)
        self._bridge.provide("pod_report", self._on_pod_report)
        self._bridge.provide("pod_missing", self._on_pod_missing)
        self._bridge.provide("sweep_done", self._on_sweep_done)
        self._bridge.provide("neighbour_packet", self._on_neighbour_packet)
        self._bridge.provide("time_sync", self._on_time_sync)
        self._bridge.provide("node_status", self._on_node_status)

    @property
    def address(self) -> str:
        return self._bridge.address

    def connect(self, timeout: float | None = None) -> bool:
        return self._bridge.connect(timeout)

    def close(self) -> None:
        self._bridge.disconnect()

    def __enter__(self):
        self.connect()
        return self

    def __exit__(self, *exc):
        self.close()

    # ---------------------------------------------------------------- provided handlers
    # Run on the bridge's dedicated dispatcher thread (arrival order,
    # sequential) — kept cheap: just typed events onto a queue, never a
    # call back into the bridge (that would raise RuntimeError, by design).
    def _push(self, ev: BridgeEvent) -> None:
        try:
            self.events.put_nowait(ev)
        except queue.Full:
            log.error("event queue full, dropping %r", ev)

    def _on_pod_report(self, addr, report):
        self._push(PodReport(int(addr), bytes(report), time.time()))

    def _on_pod_missing(self, addr):
        self._push(PodMissing(int(addr), time.time()))

    def _on_sweep_done(self, sweep, present, total):
        self._push(SweepDone(int(sweep), int(present), int(total), time.time()))

    def _on_neighbour_packet(self, packet):
        self._push(NeighbourPacket(bytes(packet), time.time()))

    def _on_time_sync(self, utc_ms, state):
        self._push(TimeSync(int(utc_ms), str(state), time.time()))

    def _on_node_status(self, status):
        self._push(NodeStatus(**_status_from_map(status), ts=time.time()))

    def poll(self, timeout: float = 0.0) -> list[BridgeEvent]:
        """Drain whatever events have arrived (non-blocking by default)."""
        out = []
        try:
            out.append(self.events.get(timeout=timeout) if timeout else self.events.get_nowait())
            while True:
                out.append(self.events.get_nowait())
        except queue.Empty:
            pass
        return out

    # ---------------------------------------------------------------- Linux -> MCU commands
    def set_alert(self, level: int, seconds: int = 0) -> None:
        if level not in (0, 1, 2):
            raise ValueError("level must be 0 (off) / 1 (advisory) / 2 (warning)")
        self._bridge.notify("set_alert", level, seconds)

    def set_cadence(self, seconds: int) -> None:
        if seconds <= 0:
            raise ValueError("cadence must be > 0 seconds")
        self._bridge.notify("set_cadence", seconds)

    def set_pods(self, addrs: list[int]) -> None:
        self._bridge.notify("set_pods", list(int(a) for a in addrs))

    def set_armed(self, armed: bool) -> None:
        self._bridge.notify("set_armed", bool(armed))

    def set_embedding(self, emb: bytes) -> None:
        self._bridge.notify("set_embedding", bytes(emb))

    def get_status(self, timeout: float | None = 5.0) -> dict:
        """Blocking CALL for a fresh read (RpcError/TimeoutError/ConnectionError propagate)."""
        return _status_from_map(self._bridge.call("get_status", timeout=timeout))


__all__ = ["HeadNodeBridge", "BridgeEvent", "PodReport", "PodMissing", "SweepDone",
           "NeighbourPacket", "TimeSync", "NodeStatus", "RpcError"]
