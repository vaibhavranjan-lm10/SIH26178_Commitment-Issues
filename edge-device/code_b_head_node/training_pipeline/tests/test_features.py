"""Feature computation: formulas, masking policy, index alignment, terrain."""
import numpy as np
import pandas as pd
import pytest

from prahari_train import features as F
from prahari_train import registry

RNG = np.random.default_rng(0)


def make_primaries(n_hours=24 * 45, start="2026-06-01 07:00", cadence="1h"):
    """A plausible-looking primary frame with all 82 IDs present (values are
    arbitrary but physically ranged).  Index deliberately starts off-midnight."""
    idx = pd.date_range(start, periods=n_hours, freq=cadence)
    t = np.arange(n_hours)
    df = pd.DataFrame(index=idx)
    diurnal = np.sin(2 * np.pi * (t % 24) / 24)
    df["P13"] = 28 + 6 * diurnal + RNG.normal(0, 0.5, n_hours)
    df["P14"] = df["P13"] - 1.5 + RNG.normal(0, 0.3, n_hours)
    df["P15"] = np.clip(60 - 20 * diurnal + RNG.normal(0, 3, n_hours), 5, 100)
    df["P16"] = np.clip(df["P15"] - 5, 5, 100)
    df["P17"] = 1005 + 3 * np.sin(2 * np.pi * t / (24 * 10)) + RNG.normal(0, 0.3, n_hours)
    df["P18"] = np.abs(RNG.normal(3, 1.5, n_hours))
    df["P19"] = (RNG.normal(200, 20, n_hours)) % 360
    df["P20"] = np.clip(800 * diurnal, 0, None)
    rain = RNG.exponential(0.3, n_hours) * (RNG.random(n_hours) < 0.15)
    df["P12"] = np.cumsum(rain)
    df["P11"] = 1.0 + 0.5 * np.convolve(rain, np.exp(-np.arange(48) / 12), mode="full")[:n_hours]
    for k, base in (("P1", 0.25), ("P2", 0.28), ("P3", 0.30)):
        df[k] = np.clip(base + 0.3 * np.convolve(rain, np.exp(-np.arange(72) / 24), mode="full")[:n_hours] / 5, 0.05, 0.5)
    df["P10"] = df["P1"] + 0.02
    df["P4"], df["P5"], df["P6"] = 26 + 2 * diurnal, 25.0, 24.0
    df["P7"], df["P8"], df["P9"] = 0.3, 0.3, 0.3
    df["P22"] = np.abs(RNG.normal(40, 10, n_hours))
    df["P23"] = df["P22"] * 1.8
    df["P21"] = df["P22"] * 0.7
    for k in ("P24", "P25", "P26", "P27", "P28", "P29", "P30", "P31", "P34", "P35", "P36"):
        df[k] = np.abs(RNG.normal(1, 0.2, n_hours))
    df["P32"] = df["P13"] + 5
    df["P33"] = np.abs(RNG.normal(0.1, 0.05, n_hours))
    df["P37"], df["P38"], df["P39"], df["P40"], df["P41"] = 7.2, 10.0, 8.0, 300.0, 24.0
    df["P42"], df["P43"], df["P44"] = RNG.normal(0, 1, n_hours), RNG.normal(0, 1, n_hours), 1000 + RNG.normal(0, 1, n_hours)
    df["P45"] = 5 + 20 * (df["P1"] - 0.25)
    df["P46"] = 20 * diurnal
    df["P47"] = rain
    df["P48"] = rain
    df["P49"], df["P50"] = df["P10"], df["P2"]
    df["P51"] = 50 + 100 * (df["P11"] - 1)
    df["P52"], df["P53"] = 35.0, 22.0
    df["P54"], df["P55"] = 0.0, 0.0
    df["P56"], df["P57"], df["P58"], df["P59"] = 0.08, 0.35, 0.20, 0.10
    df["P60"], df["P61"] = -12.0, -20.0
    df["P62"] = 0.3
    df["P63"], df["P64"] = df["P13"], df["P13"] - 8
    df["P65"], df["P66"] = 1.0, -1.0
    df["P67"] = df["P17"]
    df["P68"] = 800 + 500 * diurnal
    df["P69"] = 0.0
    return df


STATIC = {"P70": 420.0, "P71": 40.0, "P72": 35.0, "P73": 25.0, "P74": 1.35, "P75": 10, "P76": 15.0,
          "P77_lat": 23.5, "P77_lon": 85.2, "P78": 12000.0, "P79": 3, "P80": 800.0, "P81": 0.0, "P82": 0.05,
          **F.terrain_from_dem(np.add.outer(np.arange(15) * 2.0, np.arange(15) * 0.5) + 400, 30.0).as_dict()}


@pytest.fixture(scope="module")
def frames():
    prim = make_primaries()
    feats = F.compute_features(prim, STATIC)
    return prim, feats


def test_all_56_ids_produced_and_index_preserved(frames):
    prim, feats = frames
    assert set(F.SECONDARY_IDS + F.TERTIARY_IDS) <= set(feats.columns)
    assert feats.index.equals(prim.index)
    assert len(feats.columns) == 39 + 17 + len(F.EXTRA_COLUMNS)


def test_no_derived_column_is_all_nan_when_inputs_present(frames):
    """The index-alignment pitfall: a numpy-built series with a RangeIndex
    aligns to nothing and yields all NaN without raising.  Off-midnight
    DatetimeIndex + every input present ⇒ every output must have data."""
    _, feats = frames
    bad = [c for c in feats.columns if feats[c].isna().all()]
    assert bad == [], f"all-NaN features (index alignment?): {bad}"


def test_cascade_and_windows_have_expected_warmup(frames):
    _, feats = frames
    # S2 needs a full 7-day window; S3 30 days; T1 72 h spin-up after start.
    assert feats["S2"].iloc[: 7 * 24 - 1].isna().all() and feats["S2"].iloc[7 * 24 + 1:].notna().all()
    assert feats["S3"].iloc[30 * 24 + 1:].notna().all()
    assert feats["T1"].iloc[: 72].isna().all()
    assert feats["T6"].iloc[30 * 24 + 72 + 24:].notna().all()  # S3 window + cascade spin-up


def test_rainfall_counter_semantics():
    idx = pd.date_range("2026-01-01", periods=6, freq="1h")
    p12 = pd.Series([10.0, 12.0, 12.0, 15.5, 0.7, 1.7], index=idx)   # reset between 15.5 and 0.7
    inc = F.rain_increment(p12)
    assert np.isnan(inc.iloc[0])
    assert list(inc.iloc[1:]) == [2.0, 0.0, 3.5, 0.7, 1.0]
    prim = pd.DataFrame({"P12": p12})
    sec = F.compute_secondary(prim, STATIC)
    assert (sec["S1"].dropna() >= 0).all()


def test_atmospheric_derivations_match_code_a_formulas():
    idx = pd.date_range("2026-01-01", periods=3, freq="1h")
    prim = pd.DataFrame({"P13": [25.0, 35.0, 40.0], "P15": [50.0, 60.0, 20.0],
                         "P18": [10.0, 5.0, 0.0], "P19": [90.0, 180.0, 0.0]}, index=idx)
    sec = F.compute_secondary(prim, STATIC)
    # Magnus, T=25 RH=50: es=3.1699 kPa → VPD≈1.585, Td≈13.86 °C
    assert sec["S13"].iloc[0] == pytest.approx(1.585, abs=0.01)
    assert sec["S14"].iloc[0] == pytest.approx(13.86, abs=0.1)
    # wind from 90° at 10 m/s → u = −10, v ≈ 0 ; from 180° at 5 → u≈0, v=+5
    assert sec["S15"].iloc[0] == pytest.approx(-10.0) and abs(sec["S16"].iloc[0]) < 1e-9
    assert abs(sec["S15"].iloc[1]) < 1e-9 and sec["S16"].iloc[1] == pytest.approx(5.0)
    # Heat index 35 °C / 60 % ≈ 45 °C (NWS table: 95 °F / 60 % → 114 °F ≈ 45.6 °C)
    assert sec["S21"].iloc[1] == pytest.approx(45.6, abs=1.0)
    # below 80 °F the Steadman simple form is used and stays near T
    assert sec["S21"].iloc[0] == pytest.approx(25.5, abs=1.5)


def test_missing_input_masks_never_imputes(frames):
    prim, _ = frames
    prim2 = prim.copy()
    gap = slice(20 * 24, 20 * 24 + 3)
    prim2.iloc[gap, prim2.columns.get_loc("P12")] = np.nan
    prim2.iloc[gap, prim2.columns.get_loc("P13")] = np.nan
    feats = F.compute_features(prim2, STATIC)
    # S2 is NaN for the full 7-day window after the gap, then recovers
    assert feats["S2"].iloc[20 * 24: 27 * 24 + 2].isna().all()
    assert feats["S2"].iloc[27 * 24 + 4:].notna().all()
    # cascade resets: T1 NaN for 72 h after the gap, then recovers
    assert feats["T1"].iloc[20 * 24: 20 * 24 + 3 + 72].isna().all()
    assert feats["T1"].iloc[20 * 24 + 3 + 80:].notna().all()
    # a channel that is entirely absent → its derived features all NaN, nothing else affected
    prim3 = prim.drop(columns=["P11"])
    feats3 = F.compute_features(prim3, STATIC)
    assert feats3["S4"].isna().all() and feats3["S5"].isna().all()
    assert feats3["S13"].notna().sum() == feats3.shape[0]


def test_terrain_from_dem_plane():
    # plane rising 1 m per 10 m northward (rows increase southward → descends to the south)
    cell = 10.0
    dem = np.add.outer(-np.arange(9) * 1.0, np.zeros(9)) + 100
    t = F.terrain_from_dem(dem, cell)
    assert t.slope_deg == pytest.approx(np.degrees(np.arctan(0.1)), abs=1e-6)
    assert t.aspect_sin == pytest.approx(0.0, abs=1e-9) and t.aspect_cos == pytest.approx(-1.0)  # faces south
    # descends to the east → aspect east → sin=1, cos=0
    dem_e = np.add.outer(np.zeros(9), -np.arange(9) * 2.0) + 100
    t2 = F.terrain_from_dem(dem_e, cell)
    assert t2.aspect_sin == pytest.approx(1.0) and abs(t2.aspect_cos) < 1e-9
    assert t2.flow_accum_m2_per_m >= 0 and t2.dist_channel_m >= 0
    # a valley line through the centre collects flow
    valley = np.abs(np.arange(9) - 4)[None, :] * 3.0 + np.arange(9)[:, None] * 0.5
    t3 = F.terrain_from_dem(valley + 100, cell, channel_threshold_cells=5)
    assert t3.flow_accum_m2_per_m > t2.flow_accum_m2_per_m
    assert t3.dist_channel_m == 0.0
    with pytest.raises(ValueError):
        F.terrain_from_dem(np.zeros((4, 4)), cell)


def test_fwi_equations_monotone():
    idx = pd.date_range("2026-01-01", periods=3, freq="1h")
    ffmc = pd.Series([80.0, 90.0, 95.0], index=idx)
    isi_calm = F._isi(ffmc, pd.Series(0.0, index=idx), pd.Series(0.0, index=idx))
    isi_windy = F._isi(ffmc, pd.Series(5.0, index=idx), pd.Series(0.0, index=idx))
    assert (isi_calm.diff().dropna() > 0).all() and (isi_windy > isi_calm).all()
    bui = F._bui(pd.Series([10.0, 40.0, 80.0], index=idx), pd.Series([100.0, 300.0, 500.0], index=idx))
    assert (bui.diff().dropna() > 0).all()
    fwi = F._fwi(isi_windy, bui)
    assert (fwi.diff().dropna() > 0).all() and np.isfinite(fwi).all()


def test_assemble_trained_channels(frames):
    prim, feats = frames
    prim2 = prim.copy()
    prim2.iloc[5, prim2.columns.get_loc("P22")] = np.nan
    vals, mask = F.assemble_trained_channels(prim2, feats, STATIC)
    assert list(vals.columns) == registry.trained_channels() and vals.shape[1] == 41
    assert mask.shape == vals.shape
    assert mask.iloc[5]["P22"] == 0 and vals.iloc[5]["P22"] == 0
    assert mask.iloc[6]["P22"] == 1
    assert (vals.to_numpy()[mask.to_numpy() == 0] == 0).all()
    assert np.isfinite(vals.to_numpy()).all()
    assert vals["P77_lat"].iloc[0] == 23.5 and mask["P77_lat"].all()
    assert "P19" not in vals.columns


def test_cadence_check_rejects_non_uniform_index():
    idx = pd.DatetimeIndex(["2026-01-01 00:00", "2026-01-01 01:00", "2026-01-01 03:00"])
    with pytest.raises(ValueError):
        F.compute_secondary(pd.DataFrame({"P12": [0.0, 1.0, 2.0]}, index=idx), STATIC)


def test_wind_bearing_never_reaches_output(frames):
    _, feats = frames
    assert "P19" not in feats.columns
    for c in feats.columns:
        assert registry.BY_ID.get(c.split("_")[0]) is not None
