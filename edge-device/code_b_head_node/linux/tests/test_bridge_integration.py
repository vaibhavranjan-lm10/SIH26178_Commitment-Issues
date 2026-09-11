"""Integration test for the actual MCU <-> Linux message contract.

Both real peers run: the official Python `arduino_router_bridge.Bridge`
(prahari_hn.bridge.HeadNodeBridge, the code that ships) on one side, and a
process built from the REAL mcu/src/mprpc.c + mcu/src/bridge_contract.c on
the other (the "native bridge sim" — see
../../mcu/tests/native_bridge_sim/sim_main.c for exactly what is and is
not test-only about it). They meet at a MockRouter standing in for the
real on-board `arduino-router` daemon, over the officially-documented
tcp://host:port development address — no physical board involved, and no
piece of the actual wire-format code is reimplemented in Python to make
this pass.

Ordering note: every Linux -> MCU assertion below is preceded by a
`bridge.get_status()` call. Because the sim is single-threaded and
processes one TCP connection strictly in arrival order, receiving that
call's response proves every earlier notify on the same connection has
already been applied to the sim's state — a real ordering guarantee, not
a sleep-and-hope.
"""
from __future__ import annotations

import subprocess
import sys
import time
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from mock_router import MockRouter  # noqa: E402

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from prahari_hn.bridge import HeadNodeBridge, NeighbourPacket, NodeStatus, PodMissing, PodReport, SweepDone, TimeSync  # noqa: E402

MCU = Path(__file__).resolve().parents[2] / "mcu"
BUILD_SH = MCU / "tests" / "native_bridge_sim" / "build.sh"


@pytest.fixture(scope="session")
def sim_binary(tmp_path_factory):
    out = tmp_path_factory.mktemp("native_bridge_sim") / "sim_main"
    subprocess.run(["bash", str(BUILD_SH), str(out)], check=True, capture_output=True, text=True)
    return out


@pytest.fixture
def router():
    with MockRouter() as r:
        yield r


@pytest.fixture
def sim(sim_binary, router):
    p = subprocess.Popen([str(sim_binary), "127.0.0.1", str(router.port)], stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, bufsize=1)
    line = p.stdout.readline()
    assert line.strip() == "READY", f"sim did not become ready: stdout={line!r} stderr={p.stderr.read()}"
    yield p
    p.stdin.close()
    try:
        p.wait(timeout=2)
    except subprocess.TimeoutExpired:
        p.kill()


LINUX_PROVIDED_METHODS = ["pod_report", "pod_missing", "sweep_done", "neighbour_packet", "time_sync", "node_status"]


@pytest.fixture
def bridge(router, sim):
    b = HeadNodeBridge(address=f"tcp://127.0.0.1:{router.port}")
    assert b.connect(timeout=5), "HeadNodeBridge failed to connect to the MockRouter"
    # Bridge.connect() only proves the TCP connection is up; each provide()
    # registers with the router over its own asynchronous round trip. Wait
    # for the router's registry directly rather than for MCU -> Linux
    # traffic to start flowing, which would be a race no matter how it's
    # observed (see MockRouter.wait_for_registration).
    assert router.wait_for_registration(LINUX_PROVIDED_METHODS, timeout=5), \
        "HeadNodeBridge's provide() registrations never reached the router"
    yield b
    b.close()


def send(sim, line: str) -> None:
    sim.stdin.write(line + "\n")
    sim.stdin.flush()


def dump(sim) -> dict:
    line = sim.stdout.readline().strip()
    assert line.startswith("STATE "), line
    out = {}
    for tok in line[len("STATE "):].split():
        k, _, v = tok.partition("=")
        out[k] = v
    return out


def test_registration_handshake_completed(sim):
    """READY only prints after all six MCU-provided methods registered — see fixture."""
    assert sim.poll() is None


def test_mcu_to_linux_pod_report(sim, bridge):
    send(sim, "POD 5 0102030405")
    [ev] = bridge.poll(timeout=3)
    assert isinstance(ev, PodReport) and ev.addr == 5 and ev.report == bytes.fromhex("0102030405")


def test_mcu_to_linux_pod_missing(sim, bridge):
    send(sim, "MISSING 4")
    [ev] = bridge.poll(timeout=3)
    assert isinstance(ev, PodMissing) and ev.addr == 4


def test_mcu_to_linux_sweep_done_triggers_a_cycle(sim, bridge):
    send(sim, "SWEEP 12 2 3")
    events = bridge.poll(timeout=3) + bridge.poll(timeout=3)  # sweep_done + node_status, see main.c's service_bus
    sweep = next(e for e in events if isinstance(e, SweepDone))
    assert (sweep.sweep, sweep.present, sweep.total) == (12, 2, 3)


def test_mcu_to_linux_neighbour_packet(sim, bridge):
    packet = bytes([0xB7, 0x01] + list(range(18)))
    send(sim, "NBR " + packet.hex())
    [ev] = bridge.poll(timeout=3)
    assert isinstance(ev, NeighbourPacket) and ev.packet == packet


def test_mcu_to_linux_time_sync(sim, bridge):
    send(sim, "TIME 1757400000123 locked")
    [ev] = bridge.poll(timeout=3)
    assert isinstance(ev, TimeSync) and ev.utc_ms == 1757400000123 and ev.state == "locked"


def test_mcu_to_linux_node_status_pushed_after_sweep(sim, bridge):
    send(sim, "SETPPS holdover")
    send(sim, "SWEEP 7 3 3")  # sweep_done AND node_status, matching main.c's service_bus()
    events = bridge.poll(timeout=3) + bridge.poll(timeout=3)
    assert {type(e) for e in events} == {SweepDone, NodeStatus}
    status = next(e for e in events if isinstance(e, NodeStatus))
    assert status.sweeps == 7 and status.pods == 3 and status.pps == "holdover"


def test_multiple_events_arrive_in_order(sim, bridge):
    send(sim, "MISSING 1")
    send(sim, "MISSING 2")
    send(sim, "MISSING 3")
    time.sleep(0.3)  # let all three arrive before the single poll below
    events = bridge.poll(timeout=3)
    assert [e.addr for e in events if isinstance(e, PodMissing)] == [1, 2, 3]


def test_linux_to_mcu_set_alert_and_set_cadence(sim, bridge):
    bridge.set_alert(2, 600)
    bridge.set_cadence(300)
    bridge.get_status(timeout=3)  # ordering barrier — see module docstring
    send(sim, "DUMP")
    st = dump(sim)
    assert st["alert_level"] == "2" and st["alert_seconds"] == "600" and st["cadence"] == "300"


def test_linux_to_mcu_set_pods_set_armed_set_embedding(sim, bridge):
    bridge.set_pods([1, 4, 5])
    bridge.set_armed(True)
    emb = bytes(range(16))
    bridge.set_embedding(emb)
    bridge.get_status(timeout=3)
    send(sim, "DUMP")
    st = dump(sim)
    assert st["n_pods"] == "3" and st["pods"] == "1,4,5"
    assert st["armed"] == "1"
    assert st["emb"] == emb.hex()


def test_linux_to_mcu_set_alert_rejects_bad_level(bridge):
    with pytest.raises(ValueError):
        bridge.set_alert(3, 10)


def test_get_status_call_reaches_mcu_and_reflects_state(sim, bridge):
    send(sim, "SETPPS locked")
    send(sim, "SWEEP 40 3 3")
    bridge.poll(timeout=3)  # drain the sweep_done/node_status pushes
    bridge.poll(timeout=3)
    status = bridge.get_status(timeout=3)
    assert status == {"sweeps": 40, "pods": 3, "pps": "locked", "drift_ppm": 0, "batt_mv": 12800}


def test_full_round_trip_command_then_telemetry_reflects_it(sim, bridge):
    """The scenario the task actually describes: a duty-cycle-change command
    from Linux, followed by MCU telemetry that would in reality result from
    it (the sim does not simulate RS-485 timing, so this checks the
    contract carries the intent correctly end to end, both directions)."""
    bridge.set_cadence(300)
    bridge.set_alert(1, 120)
    bridge.get_status(timeout=3)
    send(sim, "DUMP")
    st = dump(sim)
    assert st["cadence"] == "300" and st["alert_level"] == "1" and st["alert_seconds"] == "120"
    send(sim, "SWEEP 1 3 3")
    events = bridge.poll(timeout=3) + bridge.poll(timeout=3)
    sweep = next(e for e in events if isinstance(e, SweepDone))
    assert (sweep.sweep, sweep.present, sweep.total) == (1, 3, 3)
