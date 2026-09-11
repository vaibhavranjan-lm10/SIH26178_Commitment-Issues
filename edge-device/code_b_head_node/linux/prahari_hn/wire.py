"""Decoder for the pod report payload (Code A's wire.h/wire.c, version 1),
as carried in ``$POD <addr> <hex>``.  This is the one Code B -> Code A
dependency CLAUDE.md permits: the wire *protocol*, re-expressed in Python
byte for byte; nothing about pod behaviour is assumed.

Layout (little-endian):
  ver(1) position(1) flags(1) cycle(2) rail_mv(2) xdcr_fault(2) xdcr_nopower(2) xdcr_range(2)
  n_primary(1) then n × [id(1) flags(1) value(int32)]      id = P-number 1..46
  n_derived(1) then n × [id(1) flags(1) value(int32)]      id = S-number (13,14,15,16,21)
Values are milli-units of the taxonomy unit (calib.h: "milli-units throughout").
"""
from __future__ import annotations

import struct
from dataclasses import dataclass, field

WIRE_VERSION = 1
POSITIONS = ("S1", "S2", "S3", "G", "U", "C")

# xdcr.h channel flags
F_VALID, F_RAW, F_FAULT, F_STALE, F_NOPOWER, F_RANGE, F_UNCAL, F_ANOMALY = (1 << i for i in range(8))
RF_WAKE, RF_RAIL_VALID = 1, 2
UNUSABLE = F_RAW | F_FAULT | F_NOPOWER | F_UNCAL     # value must not enter the column (→ masked)


class WireError(ValueError):
    pass


@dataclass
class Channel:
    id: int
    flags: int
    value_milli: int

    @property
    def usable(self) -> bool:
        return bool(self.flags & F_VALID) and not (self.flags & UNUSABLE)

    @property
    def value(self) -> float:
        return self.value_milli / 1000.0


@dataclass
class Report:
    position: int
    flags: int
    cycle: int
    rail_mv: int
    xdcr_fault: int
    xdcr_nopower: int
    xdcr_range: int
    primary: list[Channel] = field(default_factory=list)
    derived: list[Channel] = field(default_factory=list)

    @property
    def position_name(self) -> str:
        return POSITIONS[self.position] if self.position < len(POSITIONS) else f"?{self.position}"

    @property
    def wake(self) -> bool:
        return bool(self.flags & RF_WAKE)


def decode_report(buf: bytes) -> Report:
    if len(buf) < 15:
        raise WireError("short report")
    ver, pos, flags, cycle, rail, fault, nopow, rng, n_p = struct.unpack_from("<BBBHHHHHB", buf, 0)
    if ver != WIRE_VERSION:
        raise WireError(f"wire version {ver}")
    n = 14
    if n_p > 46 or n + n_p * 6 + 1 > len(buf):
        raise WireError("bad primary count")
    prim = []
    for _ in range(n_p):
        pid, fl, val = struct.unpack_from("<BBi", buf, n)
        n += 6
        if not 1 <= pid <= 46:
            raise WireError(f"primary id {pid}")
        prim.append(Channel(pid, fl, val))
    n_d = buf[n]
    n += 1
    if n_d > 5 or n + n_d * 6 > len(buf):
        raise WireError("bad derived count")
    der = []
    for _ in range(n_d):
        sid, fl, val = struct.unpack_from("<BBi", buf, n)
        n += 6
        der.append(Channel(sid, fl, val))
    if n != len(buf):
        raise WireError("trailing bytes")
    return Report(pos, flags, cycle, rail, fault, nopow, rng, prim, der)


def encode_report(r: Report) -> bytes:
    """Inverse of decode_report — used by tests and the harness to fabricate pod reports."""
    out = bytearray(struct.pack("<BBBHHHHHB", WIRE_VERSION, r.position, r.flags, r.cycle, r.rail_mv,
                                r.xdcr_fault, r.xdcr_nopower, r.xdcr_range, len(r.primary)))
    for c in r.primary:
        out += struct.pack("<BBi", c.id, c.flags, c.value_milli)
    out.append(len(r.derived))
    for c in r.derived:
        out += struct.pack("<BBi", c.id, c.flags, c.value_milli)
    return bytes(out)
