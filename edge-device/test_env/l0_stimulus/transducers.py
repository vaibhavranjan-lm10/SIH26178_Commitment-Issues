"""Physical quantity -> electrical stimulus, per transducer in the pod overlay.

Every function here converts an SI physical value (from physical.py) into
the electrical thing the transducer would actually put on its wire, and
declares the electrical envelope real hardware can produce on that wire.
Renode feeds these into Code A's real drivers:

  class C (ADC)   -> a 12-bit count 0..4095 (ref = 3.3 V) at an adc1 channel.
                     Code A's xdcr_adc.c reads it, adc_raw_to_millivolts_dt
                     scales it, and the EEPROM two-point calibration
                     (eeprom.py, matched to the transfer function here)
                     turns millivolts back into the parameter unit.
  class D (pulse) -> a GPIO edge train (idle level + list of transitions).
                     Code A's xdcr_pulse.c debounces and counts / rates it.
  class A (I2C)   -> the exact register-read response bytes (with the CRC
                     the Zephyr driver checks) for SHT4x / LPS22HB / LIS2DH.
  class B (UART)  -> the PMS7003 32-byte frames Code A's UART sensor driver
                     parses.

The overlay this matches is code_a_pod_firmware/app/boards/nucleo_l053r8.overlay
(the size stand-in; the real L051 board will be pinned the same way).
Channel / pin / address / range numbers below are transcribed from it and
from the Zephyr driver sources, not invented.
"""
from __future__ import annotations

import math
from dataclasses import dataclass, field
from typing import Callable

import numpy as np

from .physical import PhysicalState

# ---------------------------------------------------------------- ADC envelope
ADC_RES_BITS = 12                       # nucleo_l053r8.overlay: zephyr,resolution = <12>
ADC_COUNTS_MAX = (1 << ADC_RES_BITS) - 1
ADC_VREF_MV = 3300                      # ADC_REF_INTERNAL, st,stm32-adc vref-mv default 3300
ADC_VREF_UV = ADC_VREF_MV * 1000
ADC_GAIN = 1.0                          # zephyr,gain = "ADC_GAIN_1"


def volts_to_count(v: float) -> int:
    """Ideal 12-bit conversion of a pin voltage. Raises if the pin voltage is
    outside what a 0..3.3 V single-ended ADC input can represent -- an ADC
    stimulus that ever left this range would silently misrepresent what real
    hardware could produce (the exact failure this generator's tests guard)."""
    if not (0.0 <= v <= ADC_VREF_MV / 1000.0 + 1e-9):
        raise ValueError(f"pin voltage {v:.4f} V outside [0, {ADC_VREF_MV/1000:.3f}] V")
    return int(round(v / (ADC_VREF_MV / 1000.0) * ADC_COUNTS_MAX))


def count_to_uv(count: int) -> int:
    return int(round(count / ADC_COUNTS_MAX * ADC_VREF_UV))


# ---------------------------------------------------------------- CRCs (from the drivers)
def crc8(data: bytes, poly: int, init: int) -> int:
    crc = init
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ poly) & 0xFF if (crc & 0x80) else (crc << 1) & 0xFF
    return crc


def sht4x_crc(word: int) -> int:
    # sht4x.h: SHT4X_CRC_POLY 0x31, SHT4X_CRC_INIT 0xFF, over the 2 big-endian data bytes
    return crc8(word.to_bytes(2, "big"), 0x31, 0xFF)


# ---------------------------------------------------------------- class C transfer functions
@dataclass
class AdcChannel:
    """One analogue transducer: which adc1 channel, which P-id, the physical
    quantity it reads, its probe transfer function (physical -> pin volts),
    a matched two-point calibration (so Code A inverts it), and the volt
    window the front end can actually swing."""
    name: str
    channel: int
    param: int
    positions: tuple[str, ...]
    quantity: str                       # attribute of PhysicalState to read
    to_volts: Callable[[float], float]
    v_min: float                        # electrical swing limits of the conditioned signal
    v_max: float
    # two calibration points as (physical_value, pin_volts); ref stored as milli-units
    cal_points: tuple[tuple[float, float], tuple[float, float]]
    ref_unit_milli: float = 1000.0      # physical -> milli-unit factor for the ref side


def _lin(x, x0, y0, x1, y1):
    return y0 + (y1 - y0) * (x - x0) / (x1 - x0)


# Capacitive soil-moisture probe: higher voltage dry, lower wet (generic FDR probe,
# conditioned to 1.0 V saturated .. 2.6 V oven-dry).
def _soil_v(theta: float, v_dry=2.60, v_sat=1.00, th_dry=0.05, th_sat=0.50) -> float:
    return float(np.clip(_lin(theta, th_dry, v_dry, th_sat, v_sat), min(v_sat, v_dry), max(v_sat, v_dry)))


# Vibrating-wire / 4-20 mA piezometer through a 165 ohm sense + 2/3 divider:
# 0.33 V at 0 kPa .. 3.00 V at 120 kPa full scale.
def _pore_v(kpa: float) -> float:
    return float(np.clip(_lin(kpa, 0.0, 0.33, 120.0, 3.00), 0.0, 3.30))


# Silicon pyranometer, 0..2.0 V for 0..1500 W/m2.
def _pyrano_v(wm2: float) -> float:
    return float(np.clip(wm2 / 1500.0 * 2.0, 0.0, 3.30))


# Potentiometric wind vane, 0..3.3 V around 0..360 deg (overlay flags the
# class C-vs-D conflict; modelled analogue here).
def _vane_v(deg: float) -> float:
    return float(np.clip(deg % 360.0 / 360.0 * 3.30, 0.0, 3.30))


# Analogue MOX element, ~0.10 V clean .. ~2.80 V high-VOC; TVOC index 0..500.
def _mox_v(idx: float) -> float:
    return float(np.clip(_lin(idx, 0.0, 0.10, 500.0, 2.80), 0.0, 3.30))


ADC_CHANNELS: tuple[AdcChannel, ...] = (
    AdcChannel("soil_10cm", 0, 1, ("S1",), "vwc_s1", _soil_v, 1.00, 2.60,
               ((0.10, _soil_v(0.10)), (0.45, _soil_v(0.45)))),
    AdcChannel("soil_40cm", 1, 2, ("S2",), "vwc_s1", _soil_v, 1.00, 2.60,
               ((0.10, _soil_v(0.10)), (0.45, _soil_v(0.45)))),
    AdcChannel("soil_100cm", 4, 3, ("S3",), "vwc_s1", _soil_v, 1.00, 2.60,
               ((0.10, _soil_v(0.10)), (0.45, _soil_v(0.45)))),
    AdcChannel("surface_soil", 5, 10, ("G",), "vwc_surface", _soil_v, 1.00, 2.60,
               ((0.10, _soil_v(0.10)), (0.45, _soil_v(0.45)))),
    AdcChannel("pore_pressure", 6, 45, ("G",), "pore_kpa", _pore_v, 0.30, 3.05,
               ((5.0, _pore_v(5.0)), (110.0, _pore_v(110.0)))),
    AdcChannel("mox_gas", 7, 31, ("U",), "tvoc_index", _mox_v, 0.05, 2.85,
               ((20.0, _mox_v(20.0)), (300.0, _mox_v(300.0)))),
    AdcChannel("pyranometer", 8, 20, ("C",), "solar_wm2", _pyrano_v, 0.0, 2.05,
               ((100.0, _pyrano_v(100.0)), (1200.0, _pyrano_v(1200.0)))),
    AdcChannel("wind_vane", 9, 19, ("U", "C"), "wind_dir_deg", _vane_v, 0.0, 3.30,
               ((30.0, _vane_v(30.0)), (300.0, _vane_v(300.0)))),
)

# VREFINT: Code A reads its own 3.3 V rail via ADC channel 17 (st,stm32-vref,
# vrefint-cal-mv = 3000). The Zephyr driver computes vref = cal_mv * CAL / raw,
# so a raw that reports a healthy rail is raw = cal_mv * CAL / rail_mv.
VREFINT_CHANNEL = 17
VREFINT_CAL_MV = 3000
VREFINT_CAL_TYPICAL = 1655             # factory VREFINT_CAL for STM32L0 at 3.0 V VDDA (RM0451)


def vrefint_count(rail_mv: float, cal: int = VREFINT_CAL_TYPICAL) -> int:
    raw = round(VREFINT_CAL_MV * cal / rail_mv)
    return int(min(ADC_COUNTS_MAX, max(0, raw)))


# ---------------------------------------------------------------- class D pulse
@dataclass(frozen=True)
class PulseChannel:
    name: str
    port: str
    pin: int
    param: int
    positions: tuple[str, ...]
    idle_level: int                    # GPIO_PULL_UP | GPIO_ACTIVE_LOW -> idle high (1)
    active_level: int                 # pulled to this for a pulse
    debounce_ms: int
    pulse_low_ms: float               # contact-closure duration of one real pulse
    min_gap_ms: float                 # electrical floor: a reed switch cannot bounce faster
    bounce_edges: int = 0             # extra fast transitions per pulse (contact chatter)


PULSE_CHANNELS: tuple[PulseChannel, ...] = (
    # rain_gauge: gpioa 8, (GPIO_PULL_UP | GPIO_ACTIVE_LOW), debounce 10 ms, mode count -> P12
    PulseChannel("rain_gauge", "gpioPortA", 8, 12, ("G",), 1, 0, 10, 6.0, 0.3, bounce_edges=2),
    # anemometer: gpioa 11, (GPIO_PULL_UP | GPIO_ACTIVE_LOW), debounce 2 ms, mode rate -> P18
    PulseChannel("anemometer", "gpioPortA", 11, 18, ("U", "C"), 1, 0, 2, 8.0, 0.3, bounce_edges=0),
)

RAIN_TIP_MM = 0.2                      # tipping-bucket resolution
ANEMOMETER_HZ_PER_MS = 1.0 / 0.75     # pulses/s per m/s (0.75 m/s per Hz cup factor)


# ---------------------------------------------------------------- class A I2C
@dataclass(frozen=True)
class I2cSensor:
    name: str
    addr: int
    params: tuple[int, ...]
    positions: tuple[str, ...]
    kind: str


I2C_SENSORS: tuple[I2cSensor, ...] = (
    I2cSensor("sht4x", 0x44, (13, 15), ("U",), "sht4x"),        # air T/RH understory
    I2cSensor("sht4x_canopy", 0x44, (14, 16), ("C",), "sht4x"),
    I2cSensor("lps22hb", 0x5C, (17,), ("U",), "lps22hb"),        # barometric pressure
    I2cSensor("lis2dh", 0x18, (42, 43, 44), ("G",), "lis2dh"),   # mast tilt / vibration
)


def sht4x_frame(temp_c: float, rh_pct: float) -> bytes:
    """6 bytes [T_hi T_lo T_crc RH_hi RH_lo RH_crc], the read response after a
    measure command. Inverse of sht4x.c: val1 = t_sample*175/0xFFFF - 45 and
    val1 = rh_sample*125/0xFFFF - 6."""
    t_ticks = int(round((temp_c + 45.0) * 0xFFFF / 175.0))
    rh_ticks = int(round((rh_pct + 6.0) * 0xFFFF / 125.0))
    t_ticks = max(0, min(0xFFFF, t_ticks))
    rh_ticks = max(0, min(0xFFFF, rh_ticks))
    out = bytearray()
    for w in (t_ticks, rh_ticks):
        out += w.to_bytes(2, "big")
        out.append(sht4x_crc(w))
    return bytes(out)


def sht4x_decode(frame: bytes) -> tuple[float, float]:
    t = int.from_bytes(frame[0:2], "big")
    rh = int.from_bytes(frame[3:5], "big")
    return t * 175.0 / 0xFFFF - 45.0, rh * 125.0 / 0xFFFF - 6.0


LPS22HB_PRESS_OUT_XL = 0x28            # burst read start, auto-increment


def lps22hb_frame(pressure_hpa: float, temp_c: float) -> bytes:
    """5 bytes [P_xl P_l P_h T_l T_h] from a burst read at 0x28. lps22hb.c:
    press = raw >> 12 (with fraction), i.e. 4096 LSB/hPa; temp = raw / 100."""
    p_raw = int(round(pressure_hpa * 4096.0)) & 0xFFFFFF
    t_raw = int(round(temp_c * 100.0)) & 0xFFFF
    return bytes([p_raw & 0xFF, (p_raw >> 8) & 0xFF, (p_raw >> 16) & 0xFF,
                  t_raw & 0xFF, (t_raw >> 8) & 0xFF])


def lps22hb_decode(frame: bytes) -> tuple[float, float]:
    p_raw = frame[0] | (frame[1] << 8) | (frame[2] << 16)
    t_raw = frame[3] | (frame[4] << 8)
    if t_raw >= 0x8000:
        t_raw -= 0x10000
    return p_raw / 4096.0, t_raw / 100.0


LIS2DH_OUT_X_L = 0x28                  # burst read start; |0x80 for auto-increment
# lis2dh.c ±2 g (FS idx 0), NORMAL mode 10-bit left-justified:
#   ACCEL_SCALE(1600) = ((9806650 * 1600) >> 14) // 100
_LIS2DH_SCALE = ((9806650 * 1600) >> 14) // 100          # micro-(m/s^2) per (raw>>4) LSB
# sensor_xdcr.c to_param_units accel: (milli_ms2 * 100000) // 980665  -> mg


def _mg_to_raw16(mg: float) -> int:
    # forward (driver): milli_ms2 = ((raw>>4) * _LIS2DH_SCALE) / 1000
    #                   mg        = (milli_ms2 * 100000) // 980665
    # invert:
    milli_ms2 = mg * 980665.0 / 100000.0
    step = milli_ms2 * 1000.0 / _LIS2DH_SCALE           # this is (raw>>4)
    raw16 = int(round(step)) << 4
    return max(-32768, min(32767, raw16))


def _raw16_to_mg(raw16: int) -> float:
    if raw16 >= 0x8000:
        raw16 -= 0x10000
    milli_ms2 = ((raw16 >> 4) * _LIS2DH_SCALE) / 1000.0
    return milli_ms2 * 100000.0 / 980665.0


def lis2dh_frame(mg_xyz: tuple[float, float, float]) -> bytes:
    out = bytearray()
    for mg in mg_xyz:
        raw16 = _mg_to_raw16(mg) & 0xFFFF
        out += bytes([raw16 & 0xFF, (raw16 >> 8) & 0xFF])       # little-endian per axis
    return bytes(out)


def lis2dh_decode(frame: bytes) -> tuple[float, float, float]:
    return tuple(_raw16_to_mg(frame[2 * i] | (frame[2 * i + 1] << 8)) for i in range(3))


# ---------------------------------------------------------------- class B UART (PMS7003)
@dataclass(frozen=True)
class UartSensor:
    name: str
    params: tuple[int, ...]
    positions: tuple[str, ...]
    baud: int
    kind: str


UART_SENSORS: tuple[UartSensor, ...] = (
    UartSensor("pms7003", (21, 22, 23), ("U",), 9600, "pms7003"),
)


def pms7003_frame(pm1: float, pm25: float, pm10: float) -> bytes:
    """32-byte PMS7003 frame: 0x42 0x4D, 2-byte frame length, then big-endian
    16-bit fields. pms7003.c reads the 30 bytes after the start bytes and
    takes buffer[8:10]/[10:12]/[12:14] as pm1.0/pm2.5/pm10 (atm)."""
    body = bytearray(30)                         # 30 bytes after the 0x42 0x4D start (what the driver reads)
    def put(off, val):
        v = max(0, min(0xFFFF, int(round(val))))
        body[off] = (v >> 8) & 0xFF
        body[off + 1] = v & 0xFF
    put(0, 28)                                   # frame length field = 28 (0x001C)
    put(2, pm1); put(4, pm25); put(6, pm10)      # CF=1 concentrations
    put(8, pm1); put(10, pm25); put(12, pm10)    # atmospheric concentrations (what the driver reads)
    # particle counts 14..25, version/error 26..27, checksum 28..29
    frame = bytearray([0x42, 0x4D]) + body
    checksum = sum(frame[:30]) & 0xFFFF
    frame[30] = (checksum >> 8) & 0xFF
    frame[31] = checksum & 0xFF
    return bytes(frame)


def pms7003_decode(frame: bytes) -> tuple[float, float, float]:
    b = frame[2:]                                # after the 0x42 0x4D start bytes
    return ((b[8] << 8) | b[9], (b[10] << 8) | b[11], (b[12] << 8) | b[13])


# ---------------------------------------------------------------- convenience registry
TRANSDUCERS = {
    "adc": ADC_CHANNELS,
    "pulse": PULSE_CHANNELS,
    "i2c": I2C_SENSORS,
    "uart": UART_SENSORS,
}

POSITIONS = ("S1", "S2", "S3", "G", "U", "C")


def transducers_at(position: str):
    """Every transducer the overlay populates at `position`, across all classes."""
    out = {"adc": [], "pulse": [], "i2c": [], "uart": []}
    for c in ADC_CHANNELS:
        if position in c.positions:
            out["adc"].append(c)
    for c in PULSE_CHANNELS:
        if position in c.positions:
            out["pulse"].append(c)
    for c in I2C_SENSORS:
        if position in c.positions:
            out["i2c"].append(c)
    for c in UART_SENSORS:
        if position in c.positions:
            out["uart"].append(c)
    return out


def rain_tip_times_s(states: list[PhysicalState], dt_s: float) -> list[float]:
    """Virtual times (s) at which the tipping bucket tips, from the rain-rate
    trajectory: one tip per RAIN_TIP_MM of accumulated depth."""
    tips: list[float] = []
    carry = 0.0
    for k in range(1, len(states)):
        seg_mm = 0.5 * (states[k - 1].rain_rate_mm_h + states[k].rain_rate_mm_h) * (dt_s / 3600.0)
        carry += seg_mm
        t0 = (k - 1) * dt_s
        while carry >= RAIN_TIP_MM:
            carry -= RAIN_TIP_MM
            frac = 1.0 - carry / max(seg_mm, 1e-9) if seg_mm > 0 else 0.5
            tips.append(t0 + frac * dt_s)
    return tips


def anemometer_rate_intervals(states: list[PhysicalState], dt_s: float):
    """Piecewise-constant pulse frequency (Hz) over each acquisition window --
    the right electrical description of a reed switch turning many times a
    second: enumerating every one of millions of edges for a multi-day run
    is neither useful nor loadable. rate = wind_ms * ANEMOMETER_HZ_PER_MS,
    always well above the 2 ms debounce floor (a 500 Hz ceiling is imposed
    so a wild gust can never imply an un-physical edge spacing)."""
    intervals = []
    for k in range(1, len(states)):
        hz = 0.5 * (states[k - 1].wind_ms + states[k].wind_ms) * ANEMOMETER_HZ_PER_MS
        hz = min(hz, 500.0)
        intervals.append([int((k - 1) * dt_s * 1e9), round(hz, 3)])
    return intervals


def anemometer_pulse_times_s(states: list[PhysicalState], dt_s: float) -> list[float]:
    """One pulse per revolution; instantaneous rate = wind_ms * ANEMOMETER_HZ_PER_MS,
    integrated so faster wind packs pulses closer (never below the debounce floor)."""
    times: list[float] = []
    phase = 0.0
    for k in range(1, len(states)):
        hz0 = states[k - 1].wind_ms * ANEMOMETER_HZ_PER_MS
        hz1 = states[k].wind_ms * ANEMOMETER_HZ_PER_MS
        n_sub = 20
        for j in range(n_sub):
            f = (j + 0.5) / n_sub
            hz = hz0 + (hz1 - hz0) * f
            phase += hz * (dt_s / n_sub)
            while phase >= 1.0:
                phase -= 1.0
                times.append((k - 1) * dt_s + f * dt_s)
    return times


def build_pulse_edges(channel: PulseChannel, pulse_times_s: list[float], t_end_s: float):
    """(t_ns, level) edge list for one pulse channel: idle, then each pulse is a
    drop to active_level for pulse_low_ms (plus optional contact chatter),
    back to idle. Edges never come closer than min_gap_ms."""
    idle, act = channel.idle_level, channel.active_level
    edges: list[tuple[int, int]] = [(0, idle)]
    last_ns = 0
    gap_ns = int(channel.min_gap_ms * 1e6)
    for t in pulse_times_s:
        base = int(t * 1e9)
        seq: list[tuple[int, int]] = []
        # optional pre-settle chatter on the closing edge
        for i in range(channel.bounce_edges):
            seq.append((base + i * gap_ns, act if i % 2 == 0 else idle))
        seq.append((base + channel.bounce_edges * gap_ns, act))
        seq.append((base + channel.bounce_edges * gap_ns + int(channel.pulse_low_ms * 1e6), idle))
        for ns, lvl in seq:
            ns = max(ns, last_ns + gap_ns)
            if edges and edges[-1][1] == lvl:
                continue
            edges.append((ns, lvl))
            last_ns = ns
    if edges[-1][1] != idle:
        edges.append((max(last_ns + gap_ns, int(t_end_s * 1e9)), idle))
    return edges
