"""The physical trajectory must actually follow the blueprint section 11.7
narrative, so the eventual end-to-end run has a real event to detect."""
import numpy as np
import pytest

from test_env.l0_stimulus.physical import VillageScenario, physical_series


@pytest.fixture(scope="module")
def s():
    return physical_series(VillageScenario(), dt_s=300)   # 5-min grid


def test_rain_accumulation_is_monotone_non_decreasing(s):
    acc = [x.rain_accum_mm for x in s]
    assert all(b >= a - 1e-9 for a, b in zip(acc, acc[1:]))
    assert acc[-1] > 50.0, "scenario should deliver a substantial multi-day total"


def test_shallow_soil_climbs_toward_saturation_over_days(s):
    sc = VillageScenario()
    vwc = [x.vwc_s1 for x in s]
    assert vwc[0] == pytest.approx(sc.vwc_dry_s1, abs=1e-6)
    assert max(vwc) >= sc.vwc_saturation - 0.01, "shallow soil should reach ~saturation"
    # it rises, not falls, over the burst
    burst_start_idx = int(sc.main_burst.start_h * 3600 / 300)
    burst_end_idx = int(sc.main_burst.end_h * 3600 / 300)
    assert vwc[burst_end_idx] > vwc[burst_start_idx] + 0.05


def test_river_stage_rises_then_recedes_at_about_8_cm_per_hour(s):
    sc = VillageScenario()
    stage = np.array([x.stage_m for x in s])
    assert stage[0] == pytest.approx(sc.stage_base_m, abs=1e-6)
    peak = int(np.argmax(stage))
    assert 0 < peak < len(stage) - 5, "stage must crest inside the window (rise then recede)"
    assert stage[-1] < stage[peak], "stage recedes after the crest"
    rise_cm_h = np.diff(stage[:peak + 1]) / (300 / 3600) * 100
    assert rise_cm_h.max() == pytest.approx(8.0, abs=3.0), (
        f"section 11.7 says ~8 cm/h on the rising limb, got {rise_cm_h.max():.1f}")


def test_pore_pressure_and_tilt_respond_to_saturation(s):
    sc = VillageScenario()
    pore = [x.pore_kpa for x in s]
    tiltx = [abs(x.tilt_mg[0]) for x in s]
    assert pore[0] == pytest.approx(sc.pore_base_kpa, abs=0.5)
    assert max(pore) > 40.0, "pore pressure should build with shallow saturation"
    assert max(tiltx) > 10.0, "the slope should show a few tens of mg of tilt drift"
    # tilt drift is basically monotone up to the crest of saturation
    sat_peak = int(np.argmax([x.vwc_s1 for x in s]))
    assert tiltx[sat_peak] > tiltx[10] + 5.0


def test_atmosphere_reacts_to_the_burst(s):
    sc = VillageScenario()
    burst = [x for x in s if sc.main_burst.start_h <= x.t_h <= sc.main_burst.end_h]
    dry = [x for x in s if x.t_h < 24]
    assert np.mean([x.rh_pct for x in burst]) > np.mean([x.rh_pct for x in dry]) + 10
    assert np.mean([x.air_temp_c for x in burst]) < np.mean([x.air_temp_c for x in dry])
    assert np.mean([x.solar_wm2 for x in burst]) < np.mean([x.solar_wm2 for x in dry])
    assert np.mean([x.pm25_ugm3 for x in burst]) < np.mean([x.pm25_ugm3 for x in dry])
    # a synoptic pressure fall precedes the burst
    pre = [x.pressure_hpa for x in s if sc.main_burst.start_h - 12 <= x.t_h < sc.main_burst.start_h]
    early = [x.pressure_hpa for x in s if x.t_h < 12]
    assert np.mean(pre) < np.mean(early)


def test_trajectory_is_deterministic_for_a_seed():
    a = physical_series(VillageScenario(seed=11), dt_s=600)
    b = physical_series(VillageScenario(seed=11), dt_s=600)
    assert [x.tilt_mg for x in a] == [x.tilt_mg for x in b]
    assert [x.vwc_s1 for x in a] == [x.vwc_s1 for x in b]
