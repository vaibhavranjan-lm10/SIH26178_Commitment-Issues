"""The on-device trained-channel computation must agree with the offline
reference (training_pipeline/prahari_train/features.py) on the same data.
The reference is imported here ONLY as a test oracle — the runtime never
imports it."""
import sys
from pathlib import Path

import numpy as np
import pytest

from prahari_hn import features as HF

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "training_pipeline"))
pd = pytest.importorskip("pandas")
ref = pytest.importorskip("prahari_train.features")
registry = pytest.importorskip("prahari_train.registry")


def make_frame(n=60 * 24, seed=0):
    rng = np.random.default_rng(seed)
    idx = pd.date_range("2026-06-01 05:00", periods=n, freq="1h")
    t = np.arange(n); diurnal = np.sin(2 * np.pi * (t % 24) / 24)
    rain = rng.exponential(0.4, n) * (rng.random(n) < 0.15)
    df = pd.DataFrame(index=idx)
    df["P13"] = 27 + 6 * diurnal + rng.normal(0, 0.5, n); df["P14"] = df["P13"] - 1.3
    df["P15"] = np.clip(60 - 20 * diurnal + rng.normal(0, 3, n), 5, 100)
    df["P17"] = 1005 + rng.normal(0, 0.3, n); df["P18"] = np.abs(rng.normal(3, 1.5, n)); df["P19"] = rng.uniform(0, 360, n)
    df["P20"] = np.clip(800 * diurnal, 0, None); df["P12"] = np.cumsum(rain)
    df.loc[df.index[900], "P12"] = 0.3                       # a counter reset
    df["P11"] = 1 + 0.4 * np.convolve(rain, np.exp(-np.arange(48) / 12), "full")[:n]
    for k, b in (("P1", 0.22), ("P2", 0.26), ("P3", 0.3)):
        df[k] = np.clip(b + 0.05 * np.convolve(rain, np.exp(-np.arange(72) / 24), "full")[:n], 0.05, 0.5)
    df["P10"] = df["P1"] + 0.02; df["P22"] = np.abs(rng.normal(40, 10, n)); df["P23"] = df["P22"] * 1.8
    df["P32"] = df["P13"] + 5; df["P33"] = np.abs(rng.normal(0.1, 0.05, n))
    df["P42"], df["P43"], df["P44"] = rng.normal(0, 1, n), rng.normal(0, 1, n), 1000 + rng.normal(0, 1, n) + np.linspace(0, 5, n)
    for k, v in (("P47", rain), ("P49", df["P10"]), ("P51", 50 + 100 * (df["P11"] - 1)), ("P52", 35.0), ("P54", 0.0),
                 ("P57", 0.35), ("P58", 0.2), ("P60", -12.0)):
        df[k] = v
    # gaps: a pod drop-out and a lone missing hour
    df.iloc[500:506, df.columns.get_loc("P13")] = np.nan
    df.iloc[500:506, df.columns.get_loc("P15")] = np.nan
    df.iloc[1200, df.columns.get_loc("P12")] = np.nan
    return df


STATIC = {"P70": 420.0, "P71": 40.0, "P72": 35.0, "P73": 25.0, "P74": 1.35, "P75": 40, "P77_lat": 23.5, "P77_lon": 85.2,
          "S30": 5.0, "S31": 0.1, "S32": 0.99, "S33": 120.0, "S34": 300.0}


def test_runtime_channels_match_training_reference():
    df = make_frame()
    channels = registry.trained_channels()
    feats = ref.compute_features(df, STATIC)
    vals_ref, mask_ref = ref.assemble_trained_channels(df, feats, STATIC)
    prim = {c: df[c].to_numpy(float) if c in df else np.full(len(df), np.nan) for c in HF.PRIMARY_INPUTS}
    L = 512
    vals, mask = HF.compute_trained_channels(prim, STATIC, channels, L)
    vr, mr = vals_ref.to_numpy()[-L:], mask_ref.to_numpy()[-L:]
    assert vals.shape == (L, 41) and list(channels) == list(vals_ref.columns)
    bad_mask = [c for j, c in enumerate(channels) if not (mask[:, j] == mr[:, j]).all()]
    assert bad_mask == [], f"mask disagrees for {bad_mask}"
    for j, c in enumerate(channels):
        m = mask[:, j] > 0
        if m.any():
            scale = np.abs(vr[m, j]).max() + 1.0
            err = np.abs(vals[m, j] - vr[m, j]).max() / scale
            assert err < 1e-5, (c, err)
    # the masking policy shows: the P13/P15 gap breaks the cascade (T6) for 72 h after it
    j = channels.index("T6")
    assert 0 < mask[:, j].sum() < L and mask[-1, j] == 0   # the P12 gap at row 1200 masks S3→T3→T6 for 30 d


def test_masks_when_history_is_short():
    prim = {c: np.full(600, np.nan) for c in HF.PRIMARY_INPUTS}
    prim["P13"] = np.full(600, 25.0)
    vals, mask = HF.compute_trained_channels(prim, STATIC, ["P13", "S13", "S2", "T6", "S30", "P77_lat"], 512)
    assert mask[:, 0].all() and mask[:, 1].sum() == 0 and mask[:, 2].sum() == 0 and mask[:, 3].sum() == 0
    assert mask[:, 4].all() and vals[0, 4] == 5.0 and mask[:, 5].all()
    assert np.isfinite(vals).all()
