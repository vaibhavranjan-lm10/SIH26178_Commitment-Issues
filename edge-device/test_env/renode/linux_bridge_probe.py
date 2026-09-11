"""Code B's Linux side, as a native process, receiving what the emulated
head-node MCU produces over the bridge.

On the real board the MCU<->Linux bridge is the UNO Q's physical
MessagePack-RPC channel (code_b_head_node/README.md). Here Renode exposes
the emulated MCU's LPUART1 as a raw TCP socket, so this process connects
to that socket and reads the exact same MessagePack-RPC frames
bridge_contract.c emits -- carried over a socket instead of the physical
bridge, for the test.

It decodes each `pod_report` notification with the SHIPPING decoder
(prahari_hn.wire.decode_report), so a P-value printed here is the value
Code A's real firmware computed through calibration / median-of-5 /
range-gating, having crossed RS-485 to the MCU and the bridge to Linux.

Usage:  linux_bridge_probe.py <host> <port> [--seconds N] [--expect-param P]
Exit 0 once at least one pod_report with the expected param has been
decoded (or after the timeout, exit 1).
"""
from __future__ import annotations

import argparse
import socket
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "code_b_head_node" / "linux"))
import msgpack  # noqa: E402
from prahari_hn import wire as pod_wire  # noqa: E402
from prahari_hn.bridge import _status_from_map  # noqa: E402

NOTIFICATION = 2
FLAG_NAMES = {pod_wire.F_VALID: "VALID", pod_wire.F_RAW: "RAW", pod_wire.F_FAULT: "FAULT",
              pod_wire.F_STALE: "STALE", pod_wire.F_NOPOWER: "NOPOWER", pod_wire.F_RANGE: "RANGE",
              pod_wire.F_UNCAL: "UNCAL", pod_wire.F_ANOMALY: "ANOMALY"}


def flag_str(f: int) -> str:
    return "|".join(n for b, n in FLAG_NAMES.items() if f & b) or "-"


def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("host")
    ap.add_argument("port", type=int)
    ap.add_argument("--seconds", type=float, default=240.0)
    ap.add_argument("--expect-param", type=int, default=None,
                    help="exit 0 as soon as a pod_report carrying this P-id arrives")
    a = ap.parse_args(argv)

    deadline = time.monotonic() + a.seconds
    sock = None
    while sock is None and time.monotonic() < deadline:
        try:
            sock = socket.create_connection((a.host, a.port), timeout=5)
        except OSError:
            time.sleep(0.5)
    if sock is None:
        print(f"[linux] could not connect to {a.host}:{a.port}", flush=True)
        return 1
    print(f"[linux] connected to the MCU bridge socket at {a.host}:{a.port}", flush=True)
    sock.settimeout(1.0)

    unpacker = msgpack.Unpacker(raw=False, strict_map_key=False)
    seen_reports = 0
    seen_methods: dict[str, int] = {}
    while time.monotonic() < deadline:
        try:
            data = sock.recv(4096)
        except socket.timeout:
            continue
        except OSError:
            break
        if not data:
            break
        unpacker.feed(data)
        for msg in unpacker:
            if not (isinstance(msg, list) and len(msg) == 3 and msg[0] == NOTIFICATION):
                continue
            method, params = msg[1], msg[2]
            seen_methods[method] = seen_methods.get(method, 0) + 1
            if method == "pod_report":
                addr, report = int(params[0]), bytes(params[1])
                try:
                    r = pod_wire.decode_report(report)
                except pod_wire.WireError as e:
                    print(f"[linux] pod_report addr={addr}: undecodable ({e})", flush=True)
                    continue
                seen_reports += 1
                chans = {c.id: (c.value, c.flags) for c in r.primary}
                summary = " ".join(f"P{cid}={v:.3f}({flag_str(fl)})" for cid, (v, fl) in sorted(chans.items()))
                print(f"[linux] pod_report addr={addr} pos={r.position_name} cycle={r.cycle} "
                      f"rail={r.rail_mv}mV :: {summary}", flush=True)
                if a.expect_param is not None and a.expect_param in chans:
                    val, fl = chans[a.expect_param]
                    print(f"[linux] >>> P{a.expect_param} = {val} (flags {flag_str(fl)}) "
                          f"received on the Linux side, decoded from the bridged wire report", flush=True)
                    return 0
            elif method == "node_status":
                try:
                    st = _status_from_map(params[0])
                    print(f"[linux] node_status {st}", flush=True)
                except Exception:
                    pass
            elif method in ("sweep_done", "pod_missing", "time_sync"):
                print(f"[linux] {method} {params}", flush=True)
    print(f"[linux] done. methods seen: {seen_methods}; pod_reports decoded: {seen_reports}", flush=True)
    return 0 if (a.expect_param is None and seen_reports > 0) else 1


if __name__ == "__main__":
    raise SystemExit(main())
