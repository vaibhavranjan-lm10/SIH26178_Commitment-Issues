"""The eeprom.bin calibration table must be the exact inverse of the ADC
transfer functions, so Code A's real two-point calibration turns a raw
millivolt reading back into the parameter milli-unit -- and the result
lands inside the overlay's range gate rather than being flagged."""
import struct

import pytest

from test_env.l0_stimulus import eeprom as E
from test_env.l0_stimulus import transducers as T
from test_env.l0_stimulus.physical import VillageScenario, physical_series


def _crc8_07(data):
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if (crc & 0x80) else (crc << 1) & 0xFF
    return crc


@pytest.mark.parametrize("pos", ["S1", "S2", "S3", "G", "U", "C"])
def test_eeprom_blocks_have_valid_magic_and_crc(pos):
    img = E.build_eeprom_image(pos, rs485_addr=7, lora_slot=2)
    assert len(img) == E.IMAGE_SIZE
    # position block: 'P''D' ver pos crc8(first 4)
    assert img[0:2] == b"PD" and img[2] == 1 and img[3] == E.POSITION_INDEX[pos]
    assert img[4] == _crc8_07(img[0:4])
    # link cfg: 'L''K' ver addr slot crc8(first 5)
    lk = img[8:14]
    assert lk[0:2] == b"LK" and lk[2] == 1 and lk[3] == 7 and lk[4] == 2
    assert lk[5] == _crc8_07(lk[0:5])
    # calib: 'C''L' ver n, entries, crc8(all but last)
    c = img[16:]
    assert c[0:2] == b"CL" and c[2] == 1
    n = c[3]
    need = 4 + 17 * n + 1
    assert c[need - 1] == _crc8_07(c[0:need - 1])
    for i in range(n):
        param = c[4 + 17 * i]
        assert 1 <= param <= 46, "calib param must be a P1..P46 in-situ id"


def test_calibration_inverts_soil_transfer_function_within_a_milliunit():
    e = next(x for x in E.calib_entries_for("S1") if x.param == 1)
    ch = next(c for c in T.ADC_CHANNELS if c.param == 1)
    for theta in [round(0.05 + 0.02 * k, 3) for k in range(21)]:      # 0.05 .. 0.45
        v = ch.to_volts(theta)
        raw_mv = E._volts_to_adc_millivolts(v)
        milli = E.calib_apply(e, raw_mv)
        assert milli == pytest.approx(theta * 1000.0, abs=2.0), (theta, milli)


def test_every_scenario_soil_sample_stays_inside_the_overlay_range_gate():
    """Overlay: soil P1/P2/P3/P10 range-lo=0, range-hi=1000 (milli-m3/m3).
    A calibrated value outside that would get XDCR_F_RANGE-flagged."""
    states = physical_series(VillageScenario(), dt_s=60)
    for pos, quantity, param in (("S1", "vwc_s1", 1), ("G", "vwc_surface", 10)):
        e = next(x for x in E.calib_entries_for(pos) if x.param == param)
        ch = next(c for c in T.ADC_CHANNELS if c.param == param)
        for st in states:
            v = ch.to_volts(getattr(st, quantity))
            milli = E.calib_apply(e, E._volts_to_adc_millivolts(v))
            assert 0 <= milli <= 1000, f"{pos} P{param}: {milli} outside range gate [0,1000]"


def test_calib_apply_matches_firmware_rounding_rule():
    # round half away from zero, per calib.c
    e = E.CalibEntry(param=1, raw_lo=0, ref_lo=0, raw_hi=1000, ref_hi=1000)
    assert E.calib_apply(e, 500) == 500
    e2 = E.CalibEntry(param=1, raw_lo=0, ref_lo=0, raw_hi=3, ref_hi=10)
    # (1-0)*10/3 = 3.33 -> 3 ; (2-0)*10/3 = 6.67 -> 7
    assert E.calib_apply(e2, 1) == 3 and E.calib_apply(e2, 2) == 7


def test_calib_entry_serialisation_is_little_endian_int32():
    blk = E.calib_block([E.CalibEntry(1, -2422, 100, 1178, 450)])
    # header 'C''L' 1 1, then param, then 4x i32 LE
    assert blk[0:4] == b"CL\x01\x01" and blk[4] == 1
    raw_lo = struct.unpack_from("<i", blk, 5)[0]
    assert raw_lo == -2422
