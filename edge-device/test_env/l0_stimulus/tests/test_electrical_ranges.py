"""Every generated electrical artifact must stay inside what real hardware
could physically produce. An ADC stimulus that ever emitted a value outside
0-3.3 V (0..4095 counts), or a GPIO train with a non-binary level, or a
reed-switch bouncing faster than physically possible, would silently
misrepresent the device -- and Code A would then be tested against inputs
it could never see in the field."""
import json

import numpy as np
import pytest

from test_env.l0_stimulus import transducers as T
from test_env.l0_stimulus.physical import VillageScenario, physical_series, physical_state


@pytest.fixture(scope="module")
def states():
    # a fine grid AND the full scenario, so transient extremes are covered
    return physical_series(VillageScenario(), dt_s=30)


# ---------------------------------------------------------------- class C: ADC
def test_adc_transfer_functions_stay_in_front_end_and_0_3v3(states):
    for ch in T.ADC_CHANNELS:
        vals = [ch.to_volts(getattr(st, ch.quantity)) for st in states]
        assert min(vals) >= 0.0 and max(vals) <= T.ADC_VREF_MV / 1000.0, ch.name
        assert min(vals) >= ch.v_min - 1e-6 and max(vals) <= ch.v_max + 1e-6, (
            f"{ch.name}: {min(vals):.3f}..{max(vals):.3f} V outside declared "
            f"front-end swing [{ch.v_min}, {ch.v_max}]")


def test_adc_transfer_functions_clamp_beyond_physical_extremes():
    """Feed each transfer function values far outside anything the scenario
    produces; it must still never demand a pin voltage outside 0-3.3 V."""
    probes = {
        "vwc_s1": [-0.5, 0.0, 1.0, 5.0], "vwc_surface": [-0.5, 0.0, 1.0, 5.0],
        "pore_kpa": [-50.0, 0.0, 500.0, 5000.0], "tvoc_index": [-100.0, 0.0, 5000.0],
        "solar_wm2": [-100.0, 0.0, 3000.0], "wind_dir_deg": [-720.0, 0.0, 359.9, 1080.0],
    }
    for ch in T.ADC_CHANNELS:
        for x in probes[ch.quantity]:
            v = ch.to_volts(x)
            assert 0.0 <= v <= T.ADC_VREF_MV / 1000.0, f"{ch.name}({x}) -> {v} V"


def test_adc_counts_and_microvolts_in_range(states):
    for ch in T.ADC_CHANNELS:
        for st in states:
            count = T.volts_to_count(ch.to_volts(getattr(st, ch.quantity)))
            assert 0 <= count <= T.ADC_COUNTS_MAX
            uv = T.count_to_uv(count)
            assert 0 <= uv <= T.ADC_VREF_UV


def test_volts_to_count_rejects_impossible_voltage():
    with pytest.raises(ValueError):
        T.volts_to_count(3.4)
    with pytest.raises(ValueError):
        T.volts_to_count(-0.01)


def test_vrefint_count_in_range_for_plausible_rails():
    for rail_mv in (2500, 2800, 3000, 3300, 3600):
        c = T.vrefint_count(rail_mv)
        assert 0 <= c <= T.ADC_COUNTS_MAX


# ---------------------------------------------------------------- class D: pulse
@pytest.mark.parametrize("chname", ["rain_gauge", "anemometer"])
def test_pulse_edges_are_binary_ordered_and_debounce_safe(states, chname):
    ch = next(c for c in T.PULSE_CHANNELS if c.name == chname)
    dt = 30
    times = (T.rain_tip_times_s(states, dt) if chname == "rain_gauge"
             else T.anemometer_pulse_times_s(states, dt))
    edges = T.build_pulse_edges(ch, times, (len(states) - 1) * dt)
    assert edges, "no edges generated"
    levels = {lvl for _, lvl in edges}
    assert levels <= {0, 1}, levels
    ns = [t for t, _ in edges]
    assert ns == sorted(ns), "edge timestamps not monotonic"
    gaps = np.diff(ns)
    assert (gaps >= ch.min_gap_ms * 1e6 - 1).all(), (
        f"{chname}: an edge gap below the {ch.min_gap_ms} ms physical floor")
    # consecutive edges always alternate level (no repeated levels)
    for a, b in zip(edges, edges[1:]):
        assert a[1] != b[1]
    assert edges[0][1] == ch.idle_level and edges[-1][1] == ch.idle_level


def test_rain_tip_pulses_survive_debounce_as_exactly_one_count_each(states):
    """The debounce is there to eat contact chatter, not real tips. Replay the
    edge train through the firmware's debounce rule and confirm the accepted
    count equals the number of real tips."""
    ch = next(c for c in T.PULSE_CHANNELS if c.name == "rain_gauge")
    dt = 30
    tips = T.rain_tip_times_s(states, dt)
    edges = T.build_pulse_edges(ch, tips, (len(states) - 1) * dt)
    accepted = _replay_debounced_active_edges(edges, ch.idle_level, ch.active_level, ch.debounce_ms)
    assert accepted == len(tips), (accepted, len(tips))


def test_anemometer_pulse_rate_tracks_wind(states):
    ch = next(c for c in T.PULSE_CHANNELS if c.name == "anemometer")
    dt = 30
    times = T.anemometer_pulse_times_s(states, dt)
    total_s = (len(states) - 1) * dt
    mean_hz = len(times) / total_s
    mean_wind = float(np.mean([s.wind_ms for s in states]))
    assert mean_hz == pytest.approx(mean_wind * T.ANEMOMETER_HZ_PER_MS, rel=0.1)


def _replay_debounced_active_edges(edges, idle, active, debounce_ms):
    """pulse_counter_edge: an edge closer than debounce_ms to the last accepted
    edge is dropped; count edges into the active level."""
    accepted = 0
    last_ms = None
    for ns, lvl in edges:
        if lvl != active:
            continue
        t_ms = ns / 1e6
        if last_ms is None or (t_ms - last_ms) >= debounce_ms:
            accepted += 1
            last_ms = t_ms
    return accepted


# ---------------------------------------------------------------- class A: I2C
def test_i2c_bytes_and_crc_and_decoded_ranges(states):
    for st in states[::20]:
        f = T.sht4x_frame(st.air_temp_c, st.rh_pct)
        assert len(f) == 6 and all(0 <= b <= 255 for b in f)
        assert f[2] == T.sht4x_crc(int.from_bytes(f[0:2], "big"))
        assert f[5] == T.sht4x_crc(int.from_bytes(f[3:5], "big"))
        t, rh = T.sht4x_decode(f)
        assert -45.0 <= t <= 130.0 and 0.0 <= rh <= 100.0 + 1e-6
        assert t == pytest.approx(st.air_temp_c, abs=0.02)
        assert rh == pytest.approx(min(st.rh_pct, 100.0), abs=0.02)

        p = T.lps22hb_frame(st.pressure_hpa, st.air_temp_c)
        assert len(p) == 5 and all(0 <= b <= 255 for b in p)
        ph, tc = T.lps22hb_decode(p)
        assert 260.0 <= ph <= 1260.0 and ph == pytest.approx(st.pressure_hpa, abs=0.01)

        a = T.lis2dh_frame(st.tilt_mg)
        assert len(a) == 6 and all(0 <= b <= 255 for b in a)
        mg = T.lis2dh_decode(a)
        assert all(abs(x) <= 2000.0 for x in mg)          # +/-2 g full scale
        for got, want in zip(mg, st.tilt_mg):
            assert got == pytest.approx(want, abs=1.5)


def test_i2c_decoded_values_inside_overlay_range_gate(states):
    """The overlay range-lo/range-hi for the I2C params, in milli-units."""
    RANGE = {13: (-40000, 60000), 15: (0, 100000), 17: (300000, 1100000),
             42: (-2000000, 2000000), 43: (-2000000, 2000000), 44: (-2000000, 2000000)}
    for st in states[::30]:
        t, rh = T.sht4x_decode(T.sht4x_frame(st.air_temp_c, st.rh_pct))
        assert RANGE[13][0] <= t * 1000 <= RANGE[13][1]
        assert RANGE[15][0] <= rh * 1000 <= RANGE[15][1]
        ph, _ = T.lps22hb_decode(T.lps22hb_frame(st.pressure_hpa, st.air_temp_c))
        assert RANGE[17][0] <= ph * 1000 <= RANGE[17][1]
        for ax, mg in zip((42, 43, 44), T.lis2dh_decode(T.lis2dh_frame(st.tilt_mg))):
            assert RANGE[ax][0] <= mg * 1000 <= RANGE[ax][1]


# ---------------------------------------------------------------- class B: UART
def test_pms7003_frame_structure_checksum_and_decode(states):
    for st in states[::25]:
        f = T.pms7003_frame(st.pm1_ugm3, st.pm25_ugm3, st.pm10_ugm3)
        assert len(f) == 32 and f[0] == 0x42 and f[1] == 0x4D
        assert all(0 <= b <= 255 for b in f)
        assert ((f[30] << 8) | f[31]) == (sum(f[:30]) & 0xFFFF)
        pm1, pm25, pm10 = T.pms7003_decode(f)
        assert all(0 <= x <= 0xFFFF for x in (pm1, pm25, pm10))
        assert pm25 == pytest.approx(st.pm25_ugm3, abs=1.0)
