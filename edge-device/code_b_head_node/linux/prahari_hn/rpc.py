"""SUPERSEDED — mcu/src/rpc.c and hn/rpc.h (the ad hoc line protocol this
module speaks) were removed when mcu/src/bridge_contract.c replaced them
with the actual MessagePack-RPC contract; see ../README.md "The MCU <->
Linux message contract" and prahari_hn/bridge.py, the module that now
speaks it. This file is kept only because node.py has not been ported
onto bridge.py yet (../README.md "Known gap") — it no longer describes
what the firmware speaks, and must not be extended.

MCU <-> Linux command channel — the Linux end of code_b_head_node/mcu's
former rpc.h line protocol.

  Linux -> MCU:  $ALERT <level> <seconds> | $PODS a,b,.. | $CADENCE <s> | $ARM 0|1 | $EMB <hex> | $STATUS
  MCU -> Linux:  $POD <addr> <hex> | $MISSING <addr> | $SWEEP <n> <present> <total> | $NBR <hex>
                 | $TIME <utc_ms> <locked|holdover|unlocked> | $STAT k=v ...
Lines not starting with '$' are console noise and ignored.
"""
from __future__ import annotations

import collections
import os
import socket
from dataclasses import dataclass, field

ALERT_OFF, ALERT_ADVISORY, ALERT_WARNING = 0, 1, 2
RPC_EMB_MAX = 32


@dataclass
class PodReportEvent:
    addr: int
    report: bytes


@dataclass
class MissingEvent:
    addr: int


@dataclass
class SweepEvent:
    sweep: int
    present: int
    total: int


@dataclass
class NeighbourEvent:
    packet: bytes


@dataclass
class TimeEvent:
    utc_ms: int
    state: str


@dataclass
class StatEvent:
    fields: dict[str, str] = field(default_factory=dict)


RpcEvent = PodReportEvent | MissingEvent | SweepEvent | NeighbourEvent | TimeEvent | StatEvent


class RpcError(ValueError):
    pass


def parse_line(line: str) -> RpcEvent | None:
    """None for non-RPC lines; RpcError for malformed RPC lines."""
    line = line.rstrip("\r\n")
    if len(line) < 2 or line[0] != "$":
        return None
    tok = line[1:].split()
    if not tok:
        raise RpcError("empty verb")
    verb, args = tok[0], tok[1:]
    try:
        if verb == "POD" and len(args) == 2:
            return PodReportEvent(int(args[0]), bytes.fromhex(args[1]))
        if verb == "MISSING" and len(args) == 1:
            return MissingEvent(int(args[0]))
        if verb == "SWEEP" and len(args) == 3:
            return SweepEvent(int(args[0]), int(args[1]), int(args[2]))
        if verb == "NBR" and len(args) == 1:
            return NeighbourEvent(bytes.fromhex(args[0]))
        if verb == "TIME" and len(args) == 2 and args[1] in ("locked", "holdover", "unlocked"):
            return TimeEvent(int(args[0]), args[1])
        if verb == "STAT":
            return StatEvent({k: v for k, _, v in (a.partition("=") for a in args)})
    except ValueError as e:
        raise RpcError(f"malformed {verb}: {e}") from e
    raise RpcError(f"unknown or malformed line: {line!r}")


# ---- outbound, grammar exactly as rpc.c parses it
def fmt_alert(level: int, seconds: int = 0) -> str:
    if level not in (ALERT_OFF, ALERT_ADVISORY, ALERT_WARNING):
        raise RpcError("bad alert level")
    if level == ALERT_OFF:
        return "$ALERT 0\n"
    if seconds <= 0:
        raise RpcError("alert duration must be > 0")
    return f"$ALERT {level} {int(seconds)}\n"


def fmt_pods(addrs: list[int]) -> str:
    if not addrs or len(addrs) > 16 or any(not (1 <= a <= 247) for a in addrs):
        raise RpcError("bad pod address list")
    return "$PODS " + ",".join(str(a) for a in addrs) + "\n"


def fmt_cadence(seconds: int) -> str:
    if seconds <= 0:
        raise RpcError("cadence must be > 0")
    return f"$CADENCE {int(seconds)}\n"


def fmt_arm(armed: bool) -> str:
    return f"$ARM {1 if armed else 0}\n"


def fmt_emb(packet: bytes) -> str:
    if not packet or len(packet) > RPC_EMB_MAX:
        raise RpcError(f"mesh packet must be 1..{RPC_EMB_MAX} bytes")
    return "$EMB " + packet.hex() + "\n"


def fmt_status() -> str:
    return "$STATUS\n"


# ---- transports
class RpcLink:
    def send(self, line: str) -> None:
        raise NotImplementedError

    def poll(self) -> list[str]:
        """Complete lines received since the last poll (non-blocking)."""
        raise NotImplementedError

    def close(self) -> None:
        pass


class LoopbackLink(RpcLink):
    """In-memory pair for tests and the L0–L5 harness: what this side sends
    lands in ``sent``; ``inject`` queues lines as if the MCU had written them."""

    def __init__(self):
        self.sent: list[str] = []
        self._rx: collections.deque[str] = collections.deque()

    def send(self, line: str) -> None:
        self.sent.append(line)

    def inject(self, *lines: str) -> None:
        self._rx.extend(lines)

    def poll(self) -> list[str]:
        out = list(self._rx)
        self._rx.clear()
        return out


class _StreamLink(RpcLink):
    def __init__(self):
        self._buf = b""

    def _read_raw(self) -> bytes:
        raise NotImplementedError

    def poll(self) -> list[str]:
        self._buf += self._read_raw()
        lines = []
        while (i := self._buf.find(b"\n")) >= 0:
            lines.append(self._buf[:i + 1].decode("ascii", "replace"))
            self._buf = self._buf[i + 1:]
        return lines


class SerialLink(_StreamLink):
    """Raw tty on the board's internal MCU<->QRB2210 UART, stdlib only."""

    def __init__(self, device: str, baud: int = 115200):
        super().__init__()
        import termios
        import tty
        self.fd = os.open(device, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        tty.setraw(self.fd)
        attrs = termios.tcgetattr(self.fd)
        speed = getattr(termios, f"B{baud}")
        attrs[4] = attrs[5] = speed
        termios.tcsetattr(self.fd, termios.TCSANOW, attrs)

    def send(self, line: str) -> None:
        os.write(self.fd, line.encode("ascii"))

    def _read_raw(self) -> bytes:
        try:
            return os.read(self.fd, 4096)
        except BlockingIOError:
            return b""

    def close(self) -> None:
        os.close(self.fd)


class TcpLink(_StreamLink):
    """tcp://host:port — the test harness exposes the MCU side (native_sim or Renode) this way."""

    def __init__(self, url: str):
        super().__init__()
        host, _, port = url.removeprefix("tcp://").rpartition(":")
        self.sock = socket.create_connection((host, int(port)), timeout=5)
        self.sock.setblocking(False)

    def send(self, line: str) -> None:
        self.sock.sendall(line.encode("ascii"))

    def _read_raw(self) -> bytes:
        try:
            return self.sock.recv(4096)
        except (BlockingIOError, socket.timeout):
            return b""

    def close(self) -> None:
        self.sock.close()


def open_link(device: str, baud: int = 115200) -> RpcLink:
    if device == "loopback":
        return LoopbackLink()
    if device.startswith("tcp://"):
        return TcpLink(device)
    return SerialLink(device, baud)
