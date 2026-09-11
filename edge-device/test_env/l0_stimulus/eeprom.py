"""Pod EEPROM image builder.

For Code A's real pipeline to run (not just acquire raw counts), the pod
needs its persistent config block in EEPROM: the resolved position, the
RS-485 / LoRa link identity, and -- critically for this generator -- a
two-point calibration table whose entries are the exact inverse of the
probe transfer functions in transducers.py, so that a raw ADC millivolt
reading is turned back into the parameter's milli-unit and lands inside
the overlay's range-gate window.

Layouts transcribed from the firmware:
  offset 0   pod_position.c : 'P' 'D' ver(1) position crc8            (5 B)
  offset 8   link_cfg.h     : 'L' 'K' ver(1) addr slot crc8           (6 B)
  offset 16  calib.c        : 'C' 'L' ver(1) n, then n x
                              [param(1) raw_lo(i32 LE) ref_lo raw_hi ref_hi]
                              then crc8                                (4 + 17n + 1 B)
All three CRCs are pod_config_crc8 (CRC-8, poly 0x07, init 0x00).
"""
from __future__ import annotations

import struct
from dataclasses import dataclass

from .transducers import ADC_CHANNELS, ADC_COUNTS_MAX, ADC_VREF_MV

POSITION_INDEX = {"S1": 0, "S2": 1, "S3": 2, "G": 3, "U": 4, "C": 5}

CALIB_OFFSET = 16          # CONFIG_PRAHARI_EEPROM_CALIB_OFFSET default
LINK_CFG_OFFSET = 8
POS_OFFSET = 0
IMAGE_SIZE = 512


def crc8_07(data: bytes) -> int:
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if (crc & 0x80) else (crc << 1) & 0xFF
    return crc


def _volts_to_adc_millivolts(v: float) -> int:
    """What Code A's xdcr_adc.c hands to calib_apply: adc_raw_to_millivolts_dt
    of the ideal 12-bit count, gain 1, ref 3300 mV."""
    count = round(v / (ADC_VREF_MV / 1000.0) * ADC_COUNTS_MAX)
    return int(round(count * ADC_VREF_MV / ADC_COUNTS_MAX))


def position_block(position: str) -> bytes:
    b = bytearray([ord("P"), ord("D"), 1, POSITION_INDEX[position]])
    b.append(crc8_07(bytes(b)))
    return bytes(b)


def link_cfg_block(addr: int, slot: int) -> bytes:
    if not (1 <= addr <= 247):
        raise ValueError("RS-485 address must be 1..247")
    b = bytearray([ord("L"), ord("K"), 1, addr & 0xFF, slot & 0xFF])
    b.append(crc8_07(bytes(b)))
    return bytes(b)


@dataclass(frozen=True)
class CalibEntry:
    param: int
    raw_lo: int      # ADC millivolts at the low reference point
    ref_lo: int      # parameter milli-unit at the low reference point
    raw_hi: int
    ref_hi: int


def calib_entries_for(position: str) -> list[CalibEntry]:
    out: list[CalibEntry] = []
    for ch in ADC_CHANNELS:
        if position not in ch.positions:
            continue
        (p0, v0), (p1, v1) = ch.cal_points
        out.append(CalibEntry(
            param=ch.param,
            raw_lo=_volts_to_adc_millivolts(v0), ref_lo=int(round(p0 * ch.ref_unit_milli)),
            raw_hi=_volts_to_adc_millivolts(v1), ref_hi=int(round(p1 * ch.ref_unit_milli)),
        ))
    return out


def calib_block(entries: list[CalibEntry]) -> bytes:
    if len(entries) > 16:
        raise ValueError("calib table holds at most 16 entries (CALIB_MAX_ENTRIES)")
    b = bytearray([ord("C"), ord("L"), 1, len(entries)])
    for e in entries:
        b.append(e.param)
        b += struct.pack("<iiii", e.raw_lo, e.ref_lo, e.raw_hi, e.ref_hi)
    b.append(crc8_07(bytes(b)))
    return bytes(b)


def build_eeprom_image(position: str, rs485_addr: int = 1, lora_slot: int = 0,
                       size: int = IMAGE_SIZE) -> bytes:
    img = bytearray(b"\xff" * size)
    def place(off: int, blk: bytes):
        img[off:off + len(blk)] = blk
    place(POS_OFFSET, position_block(position))
    place(LINK_CFG_OFFSET, link_cfg_block(rs485_addr, lora_slot))
    place(CALIB_OFFSET, calib_block(calib_entries_for(position)))
    return bytes(img)


def calib_apply(e: CalibEntry, raw_mv: int) -> int:
    """Replica of calib.c calib_apply (round half away from zero) so the
    generator's tests can check its calibration inverts the transfer function
    the same way the firmware will."""
    span_raw = e.raw_hi - e.raw_lo
    span_ref = e.ref_hi - e.ref_lo
    num = (raw_mv - e.raw_lo) * span_ref
    half = span_raw // 2
    q = (num + half) // span_raw if (num >= 0) == (span_raw >= 0) else (num - half) // span_raw
    return int(q + e.ref_lo)
