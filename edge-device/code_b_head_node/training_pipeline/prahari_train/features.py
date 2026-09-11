"""Secondary (S1–S39) and tertiary (T1–T17) feature computation.

Every function here implements one row of the "From" column in
docs/reference/prahari_parameters.md.  The input is a DataFrame of primary
parameters (columns named by registry ID, ``P1`` … ``P82``) on a uniform
DatetimeIndex; the output is a DataFrame of the same index with one column
per secondary/tertiary ID.

Ground rules (CLAUDE.md):

* **Mask, never impute.**  A missing input is NaN and *stays* NaN through
  every derivation.  Rolling windows use ``min_periods`` equal to the full
  window (``min_coverage=1.0``) by default, so a gap in P12 breaks S2 for
  the whole 7-day window rather than quietly under-counting rain.  The
  fire-weather cascade (T1–T7) is stateful: a NaN input resets the state
  and the outputs stay NaN through a 72-hour spin-up.  That is the "an
  unbroken daily record" constraint from the spec made explicit.
* **Index alignment.**  Every derived series that passes through numpy is
  re-wrapped with the *source* index (``_wrap``).  A default RangeIndex
  silently aligns to nothing and yields all-NaN — tested for in
  tests/test_features.py.
* **Rainfall semantics.**  P12 is a cumulative *accumulation counter* (mm
  since install/reset, monotone non-decreasing).  A negative step is a
  counter reset and the step's increment is taken as the new counter
  value.  S1 is the *rate* (mm/h).  Every consumer (S2, S3, S39, T1, T2,
  T3, T8, T10) goes through ``rain_increment`` — never through P12 directly.
* **Wind / aspect** are sin/cos pairs (S15/S16, S31/S32).  The formulas for
  S13, S14, S15, S16 and S21 are the same ones Code A's derive.c uses
  (Magnus A=17.625, B=243.04; u = −s·sinθ, v = −s·cosθ; NWS Rothfusz), so
  the offline features and the pod's on-board derivations agree.
* **Simplified proxies.**  Where the spec names an index whose reference
  formulation needs data we do not have (a multi-year climatology for S5,
  a gamma fit for S39, the full CFFDRS for T1–T7, a soil-mechanics model
  for T12 …), the implementation is a documented *proxy* and says so in
  its docstring.  Nothing here claims to be the reference formulation
  unless it is (T4, T5, T6 are the published FWI-system equations).

Terrain (S30–S34) is derived from a DEM patch around the site (P70 is the
elevation *field*, not one number) via ``terrain_from_dem``; the caller
passes the per-site scalars in ``static``.

OFFLINE ONLY.  The head-node runtime (code_b_head_node/linux/) will need
its own implementation of the trained-subset features; this module is the
reference it is checked against, not the code it runs.
"""
from __future__ import annotations

import math
from dataclasses import dataclass

import numpy as np
import pandas as pd

from . import registry

# ------------------------------------------------------------------ constants
MAGNUS_A = 17.625
MAGNUS_B = 243.04
MAGNUS_ES0_KPA = 0.61094

# Soil layer thickness represented by each probe depth, metres
# (0–25 cm → P1 @10 cm, 25–70 cm → P2 @40 cm, 70–130 cm → P3 @100 cm).
LAYER_THICKNESS_M = {"P1": 0.25, "P2": 0.45, "P3": 0.60}
PROFILE_DEPTH_MM = 1000.0 * sum(LAYER_THICKNESS_M.values())  # 1300 mm
PARTICLE_DENSITY = 2.65  # g/cm³

API_DAILY_DECAY = 0.90         # S2/S3 exponential decay per day
SAR_WATER_VV_DB = -18.0        # S29 thresholds
SAR_WATER_VH_DB = -25.0
CASCADE_SPINUP_H = 72          # T1–T7 outputs NaN for this long after a reset
FFMC_INIT, DMC_INIT, DC_INIT = 85.0, 6.0, 15.0

# P75 land-cover class codes (ESA WorldCover-style; the synthetic generator
# uses the same table) → S38 fuel model class (NFFL-inspired, 0 = no fuel).
LAND_COVER_CODES = {10: "tree", 20: "shrub", 30: "grass", 40: "crop",
                    50: "built", 60: "bare", 80: "water", 90: "wetland"}
FUEL_CLASS_BY_LAND_COVER = {10: 8, 20: 5, 30: 1, 40: 3, 50: 0, 60: 0, 80: 0, 90: 3}
# T7 relative spread factor per fuel class (dimensionless, documented proxy)
FUEL_SPREAD_FACTOR = {0: 0.0, 1: 1.0, 3: 0.6, 5: 0.8, 8: 0.35}

SECONDARY_IDS = [f"S{i}" for i in range(1, 40)]
TERTIARY_IDS = [f"T{i}" for i in range(1, 18)]
EXTRA_COLUMNS = ["S23_24h"]  # second window of S23 ("3 h / 24 h"), not a new parameter


# ------------------------------------------------------------------ helpers
def cadence_hours(index: pd.DatetimeIndex) -> float:
    if len(index) < 2:
        raise ValueError("need at least two timestamps to infer cadence")
    d = np.diff(index.values).astype("timedelta64[s]").astype(float)
    step = float(np.median(d)) / 3600.0
    if not np.allclose(d / 3600.0, step, rtol=1e-6, atol=1e-9):
        raise ValueError("index must be uniformly spaced (open decision 2: resampling rule)")
    return step


def _wrap(values, like: pd.Series | pd.DataFrame, name: str | None = None) -> pd.Series:
    """Re-attach the source index to a numpy result.  THE index-alignment guard."""
    return pd.Series(np.asarray(values, dtype=float), index=like.index, name=name)


def _col(df: pd.DataFrame, pid: str) -> pd.Series:
    """Column ``pid`` or an all-NaN series on the same index (channel absent → masked)."""
    if pid in df.columns:
        return df[pid].astype(float)
    return pd.Series(np.nan, index=df.index, name=pid, dtype=float)


def _steps(hours: float, cad_h: float) -> int:
    return max(1, int(round(hours / cad_h)))


def _min_periods(n: int, min_coverage: float) -> int:
    return max(1, int(math.ceil(n * min_coverage)))


def _roll(s: pd.Series, hours: float, cad_h: float, fn: str, min_coverage: float) -> pd.Series:
    n = _steps(hours, cad_h)
    r = s.rolling(n, min_periods=_min_periods(n, min_coverage))
    return getattr(r, fn)()


def _es_kpa(t_c: pd.Series) -> pd.Series:
    return MAGNUS_ES0_KPA * np.exp(MAGNUS_A * t_c / (t_c + MAGNUS_B))


def rain_increment(p12: pd.Series) -> pd.Series:
    """Per-step rainfall (mm) from the cumulative counter P12.

    Negative steps are counter resets: the increment is the new counter value
    (rain since the reset), not a discarded sample.  First sample is NaN.
    """
    d = p12.diff()
    reset = d < 0
    d = d.where(~reset, p12)
    return d


def _decayed_sum(inc: pd.Series, window_h: float, cad_h: float, min_coverage: float) -> pd.Series:
    """Σ k^(i·cad/24) · inc[t−i] over the window; NaN if coverage below threshold."""
    n = _steps(window_h, cad_h)
    w = API_DAILY_DECAY ** (np.arange(n) * cad_h / 24.0)
    x = inc.to_numpy(dtype=float)
    ok = np.isfinite(x).astype(float)
    x0 = np.where(np.isfinite(x), x, 0.0)
    # causal convolution: out[t] = Σ_i w[i] x0[t-i]
    conv = np.convolve(x0, w, mode="full")[: len(x0)]
    cnt = np.convolve(ok, np.ones(n), mode="full")[: len(x0)]
    need = _min_periods(n, min_coverage)
    out = np.where(cnt >= need, conv, np.nan)
    out[: n - 1] = np.where(cnt[: n - 1] >= need, conv[: n - 1], np.nan) if min_coverage < 1.0 else np.nan
    return _wrap(out, inc)


# ------------------------------------------------------------------ terrain (S30–S34 from P70)
@dataclass(frozen=True)
class Terrain:
    slope_deg: float          # S30
    aspect_sin: float         # S31
    aspect_cos: float         # S32
    flow_accum_m2_per_m: float  # S33  specific catchment area
    dist_channel_m: float     # S34

    def as_dict(self) -> dict[str, float]:
        return {"S30": self.slope_deg, "S31": self.aspect_sin, "S32": self.aspect_cos,
                "S33": self.flow_accum_m2_per_m, "S34": self.dist_channel_m}


def terrain_from_dem(dem: np.ndarray, cellsize_m: float, channel_threshold_cells: int = 20) -> Terrain:
    """S30–S34 at the centre cell of a square DEM patch (P70 field).

    Slope/aspect: Horn (1981) 3×3 finite differences.  Flow accumulation:
    single-direction D8 (each cell drains to its steepest downslope
    neighbour; flats drain nowhere), counted in upslope cells and converted
    to specific catchment area (cells × cellsize).  Channel: any cell whose
    accumulation exceeds ``channel_threshold_cells``; S34 is the Euclidean
    distance from the centre to the nearest such cell (0 if the centre is
    one).  Aspect is returned as sin/cos, never a bearing.
    """
    dem = np.asarray(dem, dtype=float)
    if dem.ndim != 2 or dem.shape[0] < 3 or dem.shape[1] < 3 or dem.shape[0] % 2 == 0 or dem.shape[1] % 2 == 0:
        raise ValueError("dem must be a 2-D array with odd dimensions ≥ 3")
    ci, cj = dem.shape[0] // 2, dem.shape[1] // 2
    z = dem[ci - 1:ci + 2, cj - 1:cj + 2]
    dzdx = ((z[0, 2] + 2 * z[1, 2] + z[2, 2]) - (z[0, 0] + 2 * z[1, 0] + z[2, 0])) / (8 * cellsize_m)
    dzdy = ((z[2, 0] + 2 * z[2, 1] + z[2, 2]) - (z[0, 0] + 2 * z[0, 1] + z[0, 2])) / (8 * cellsize_m)
    slope = math.degrees(math.atan(math.hypot(dzdx, dzdy)))
    # aspect: direction the slope faces, compass bearing (0 = north, clockwise)
    if dzdx == 0 and dzdy == 0:
        asp_sin, asp_cos = 0.0, 0.0  # flat: no aspect; both components zero
    else:
        bearing = math.atan2(-dzdx, dzdy)  # radians, 0 = north (row index increases southward)
        asp_sin, asp_cos = math.sin(bearing), math.cos(bearing)

    # D8 flow accumulation
    H, W = dem.shape
    nbrs = [(-1, -1), (-1, 0), (-1, 1), (0, -1), (0, 1), (1, -1), (1, 0), (1, 1)]
    recv = -np.ones((H, W, 2), dtype=int)
    for i in range(H):
        for j in range(W):
            best, bd = None, 0.0
            for di, dj in nbrs:
                ii, jj = i + di, j + dj
                if 0 <= ii < H and 0 <= jj < W:
                    dist = cellsize_m * math.hypot(di, dj)
                    drop = (dem[i, j] - dem[ii, jj]) / dist
                    if drop > bd:
                        bd, best = drop, (ii, jj)
            if best is not None:
                recv[i, j] = best
    acc = np.zeros((H, W))
    order = sorted(((dem[i, j], i, j) for i in range(H) for j in range(W)), reverse=True)
    for _, i, j in order:  # process from high to low so upstream counts are complete
        ii, jj = recv[i, j]
        if ii >= 0:
            acc[ii, jj] += acc[i, j] + 1
    sca = acc[ci, cj] * cellsize_m
    chan = np.argwhere(acc > channel_threshold_cells)
    if len(chan) == 0:
        dist_ch = float(cellsize_m * max(H, W))  # no channel in the patch: at least patch-size away
    else:
        d = np.hypot(chan[:, 0] - ci, chan[:, 1] - cj) * cellsize_m
        dist_ch = float(d.min())
    return Terrain(slope, asp_sin, asp_cos, float(sca), dist_ch)


# ------------------------------------------------------------------ secondary
def compute_secondary(df: pd.DataFrame, static: dict[str, float] | None = None,
                      min_coverage: float = 1.0) -> pd.DataFrame:
    """S1–S39 (+ S23_24h).  ``static`` supplies per-site scalars for S30–S34
    and any P70–P82 not present as columns.  Returns a DataFrame aligned to
    ``df.index``; absent inputs propagate as NaN.
    """
    if not isinstance(df.index, pd.DatetimeIndex):
        raise TypeError("df must be indexed by a DatetimeIndex")
    cad = cadence_hours(df.index)
    static = dict(static or {})
    out = pd.DataFrame(index=df.index)

    def P(pid: str) -> pd.Series:
        if pid in df.columns:
            return df[pid].astype(float)
        if pid in static:
            return pd.Series(float(static[pid]), index=df.index, dtype=float)
        return _col(df, pid)

    # -- hydrological
    inc = rain_increment(P("P12"))
    out["S1"] = inc / cad                                        # mm/h
    out["S2"] = _decayed_sum(inc, 7 * 24, cad, min_coverage)     # mm, 7 d decayed
    out["S3"] = _decayed_sum(inc, 30 * 24, cad, min_coverage)    # mm, 30 d decayed
    out["S4"] = P("P11").diff() / cad                            # m/h
    # S5 proxy: anomaly vs the trailing 30-day median (no multi-year climatology yet)
    out["S5"] = P("P11") - _roll(P("P11"), 30 * 24, cad, "median", min(min_coverage, 0.5))
    out["S6"] = P("P1") - P("P2")
    out["S7"] = P("P2") - P("P3")
    out["S8"] = 1000.0 * (P("P1") * LAYER_THICKNESS_M["P1"] + P("P2") * LAYER_THICKNESS_M["P2"]
                          + P("P3") * LAYER_THICKNESS_M["P3"])   # mm stored in 0–130 cm
    theta_sat = _theta_sat(P("P71"), P("P73"), P("P74"))
    out["S9"] = 1000.0 * sum((theta_sat - P(k)).clip(lower=0) * LAYER_THICKNESS_M[k]
                             for k in ("P1", "P2", "P3"))         # mm deficit to saturation
    out["S10"] = _infiltration_front(P("P1"), P("P2"), P("P3"), cad)
    out["S11"] = _ksat_cosby(P("P71"), P("P73"))                 # mm/h
    out["S12"] = P("P4") - P("P6")

    # -- atmospheric (S13/S14/S15/S16/S21 mirror Code A derive.c)
    t, rh = P("P13"), P("P15").clip(lower=0.1, upper=100.0)
    es = _es_kpa(t)
    out["S13"] = es * (1.0 - rh / 100.0)                         # kPa
    gamma = np.log(rh / 100.0) + MAGNUS_A * t / (t + MAGNUS_B)
    out["S14"] = MAGNUS_B * gamma / (MAGNUS_A - gamma)           # °C
    th = np.deg2rad(P("P19"))
    out["S15"] = -P("P18") * np.sin(th)                          # m/s, meteorological convention
    out["S16"] = -P("P18") * np.cos(th)
    gust_h = max(3 * cad, 10.0 / 60.0)                           # ≥3 steps, ≥10 min
    ws = P("P18")
    out["S17"] = _roll(ws, gust_h, cad, "max", min_coverage) / (_roll(ws, gust_h, cad, "mean", min_coverage) + 0.1)
    out["S18"] = P("P14") - P("P13")
    out["S19"] = P("P16") - P("P15")
    out["S20"] = _roll(t, 24, cad, "max", min_coverage) - _roll(t, 24, cad, "min", min_coverage)
    out["S21"] = heat_index_c(t, P("P15"))
    out["S22"] = P("P68") * P("P18")                             # m²/s
    out["S23"] = P("P17") - P("P17").shift(_steps(3, cad))       # hPa / 3 h
    out["S23_24h"] = P("P17") - P("P17").shift(_steps(24, cad))  # hPa / 24 h
    out["S24"] = _rotation_rate(out["S15"], out["S16"], cad)     # deg/h

    # -- remote-sensing indices
    out["S25"] = _nd(P("P57"), P("P56"))
    out["S26"] = _nd(P("P57"), P("P58"))
    out["S27"] = _nd(P("P59"), P("P57"))
    out["S28"] = P("P52") - P("P53")
    vv, vh = P("P60"), P("P61")
    water = ((vv < SAR_WATER_VV_DB) & (vh < SAR_WATER_VH_DB)).astype(float)
    out["S29"] = water.where(vv.notna() & vh.notna())

    # -- terrain: per-site scalars from terrain_from_dem (P70 field)
    for sid in ("S30", "S31", "S32", "S33", "S34"):
        out[sid] = P(sid)
    slope_rad = np.deg2rad(out["S30"].clip(lower=0.1))
    out["S35"] = np.log((out["S33"] + 1.0) / np.tan(slope_rad))

    # -- other
    pm10 = P("P23")
    out["S36"] = (P("P22") / pm10).where(pm10 > 0)
    out["S37"] = _tilt_drift(P("P42"), P("P43"), P("P44"), cad, min_coverage)
    out["S38"] = P("P75").map(lambda c: FUEL_CLASS_BY_LAND_COVER.get(int(c)) if np.isfinite(c) else np.nan).astype(float)
    out["S39"] = _spi(inc, P("P47"), cad, min_coverage)
    return out


def _theta_sat(sand_pct: pd.Series, clay_pct: pd.Series, bulk_density: pd.Series) -> pd.Series:
    """Porosity from bulk density (φ = 1 − ρb/2.65); where P74 is absent, the
    Saxton (1986) texture form from P71/P73 is used instead.  Two computation
    paths over available inputs — not an imputation."""
    phi = 1.0 - bulk_density / PARTICLE_DENSITY
    saxton = 0.332 - 7.251e-4 * sand_pct + 0.1276 * np.log10(clay_pct.clip(lower=0.1))
    return phi.where(phi.notna(), saxton)


def _ksat_cosby(sand_pct: pd.Series, clay_pct: pd.Series) -> pd.Series:
    """Cosby et al. (1984) pedotransfer: log10 Ksat[in/h] = −0.6 + 0.0126·sand% − 0.0064·clay%.
    Spec lists P71–P74; the Cosby form uses sand and clay only (silt is the
    remainder; bulk density enters the soil block through S9's porosity)."""
    return 25.4 * 10.0 ** (-0.6 + 0.0126 * sand_pct - 0.0064 * clay_pct)


def _infiltration_front(p1, p2, p3, cad_h, rise_threshold=0.005) -> pd.Series:
    """Depth (cm) of the deepest layer whose moisture rose over the last 6 h.
    Proxy: 0 = no front, 10/40/100 = deepest rising probe.  NaN if any probe is missing
    (a missing deeper probe cannot prove the front has *not* reached it)."""
    n = _steps(6, cad_h)
    r1, r2, r3 = (s - s.shift(n) > rise_threshold for s in (p1, p2, p3))
    depth = pd.Series(0.0, index=p1.index)
    depth = depth.where(~r1, 10.0).where(~r2, 40.0).where(~r3, 100.0)
    ok = p1.notna() & p2.notna() & p3.notna() & p1.shift(n).notna() & p2.shift(n).notna() & p3.shift(n).notna()
    return depth.where(ok)


def heat_index_c(t_c: pd.Series, rh_pct: pd.Series) -> pd.Series:
    """NWS heat index (Steadman simple form below 80 °F, Rothfusz above, with
    the two NWS adjustments) — identical to Code A derive_heat_index()."""
    tf = t_c * 9.0 / 5.0 + 32.0
    rh = rh_pct.clip(lower=0.0, upper=100.0)
    hi = 0.5 * (tf + 61.0 + (tf - 68.0) * 1.2 + rh * 0.094)
    t2, r2 = tf * tf, rh * rh
    roth = (-42.379 + 2.04901523 * tf + 10.14333127 * rh - 0.22475541 * tf * rh
            - 6.83783e-3 * t2 - 5.481717e-2 * r2 + 1.22874e-3 * t2 * rh
            + 8.5282e-4 * tf * r2 - 1.99e-6 * t2 * r2)
    adj1 = ((13.0 - rh) / 4.0) * np.sqrt(((17.0 - (tf - 95.0).abs()) / 17.0).clip(lower=0))
    adj2 = ((rh - 85.0) / 10.0) * ((87.0 - tf) / 5.0)
    roth = roth.where(~((rh < 13.0) & (tf >= 80.0) & (tf <= 112.0)), roth - adj1)
    roth = roth.where(~((rh > 85.0) & (tf >= 80.0) & (tf <= 87.0)), roth + adj2)
    hi = hi.where(hi < 80.0, roth)
    return (hi - 32.0) * 5.0 / 9.0


def _rotation_rate(u: pd.Series, v: pd.Series, cad_h: float) -> pd.Series:
    """Signed angular rate (deg/h) of the wind vector, via atan2 of cross/dot
    between consecutive vectors — no unwrap needed, NaN-safe."""
    u0, v0 = u.shift(1), v.shift(1)
    ang = np.arctan2(u0 * v - v0 * u, u0 * u + v0 * v)
    return np.degrees(ang) / cad_h


def _nd(a: pd.Series, b: pd.Series) -> pd.Series:
    den = a + b
    return ((a - b) / den).where(den.abs() > 1e-9)


def _tilt_drift(x, y, z, cad_h, min_coverage) -> pd.Series:
    """|tilt| low-passed (6 h median) minus its value 24 h earlier → mg/day."""
    mag = np.sqrt(x * x + y * y + z * z)
    lp = _roll(mag, 6, cad_h, "median", min_coverage)
    return lp - lp.shift(_steps(24, cad_h))


def _spi(inc: pd.Series, p47_rate: pd.Series, cad_h: float, min_coverage: float) -> pd.Series:
    """Standardised precipitation index, 30-day scale — simplified proxy.

    Reference SPI fits a gamma distribution to a multi-decade record.  Here
    the 30-day in-situ accumulation (from P12 increments) is standardised
    against the expanding mean/std of the satellite (P47) 30-day accumulation
    history — a z-score, not a gamma quantile, and only as long a baseline as
    the P47 record delivered so far.
    """
    n = _steps(30 * 24, cad_h)
    acc_in = inc.rolling(n, min_periods=_min_periods(n, min_coverage)).sum()
    acc_sat = (p47_rate * cad_h).rolling(n, min_periods=_min_periods(n, min_coverage)).sum()
    nb = _steps(7 * 24, cad_h)  # baseline needs ≥7 d of 30-day accumulations (documented minimum)
    mu = acc_sat.expanding(min_periods=nb).mean()
    sd = acc_sat.expanding(min_periods=nb).std()
    return ((acc_in - mu) / sd).where(sd > 1e-6)


# ------------------------------------------------------------------ tertiary
def compute_tertiary(prim: pd.DataFrame, sec: pd.DataFrame, static: dict[str, float] | None = None) -> pd.DataFrame:
    cad = cadence_hours(prim.index)
    static = dict(static or {})

    def P(pid: str) -> pd.Series:
        if pid in prim.columns:
            return prim[pid].astype(float)
        if pid in static:
            return pd.Series(float(static[pid]), index=prim.index, dtype=float)
        return _col(prim, pid)

    S = lambda sid: sec[sid].astype(float)  # noqa: E731
    out = pd.DataFrame(index=prim.index)

    # -- fire weather cascade (T1–T3 simplified hourly proxies of CFFDRS; T4–T6 published equations)
    out["T1"] = _ffmc(S("S13"), S("S1"), P("P13"), P("P15"), P("P18"), cad)
    out["T2"] = _dmc(S("S2"), P("P13"), P("P15"), cad)
    out["T3"] = _dc(S("S3"), P("P13"), cad)
    out["T4"] = _isi(out["T1"], S("S15"), S("S16"))
    out["T5"] = _bui(out["T2"], out["T3"])
    out["T6"] = _fwi(out["T4"], out["T5"])
    out["T7"] = _spread_rate(out["T1"], S("S30"), S("S17"), S("S38"))

    # -- flood and landslide (proxies)
    # T8: rain exceeding the infiltration capacity; capacity → 0 as the deficit closes.
    out["T8"] = (S("S1") - S("S11") * (1.0 - np.exp(-S("S9") / 50.0))).clip(lower=0)   # mm/h
    out["T9"] = 0.4 * S("S8") / PROFILE_DEPTH_MM * 2 + 0.4 * (S("S2") / 150.0).clip(upper=1.0) + 0.2 * S("S35") / 15.0
    out["T10"] = out["T8"] / (S("S1") + 0.1)                      # fraction of rain becoming runoff
    out["T11"] = (0.35 * (S("S30") / 45.0).clip(0, 1) + 0.30 * (1.0 - (S("S9") / 200.0).clip(0, 1))
                  + 0.15 * (1.0 - (np.log10(S("S11") + 1.0) / 2.5).clip(0, 1))
                  + 0.20 * (S("S37").abs() / 5.0).clip(0, 1))
    out["T12"] = _fos(S("S30"), P("P45"), S("S9"))

    # -- pollution and heat (proxies)
    out["T13"] = S("S22") * _sigmoid(-S("S18") / 2.0) * S("S17").clip(1.0, 3.0) / 1000.0
    out["T14"] = S("S36") / (out["T13"] + 0.05)
    excess = (S("S21") - 32.0).clip(lower=0) * (1.0 + (8.0 - S("S20")).clip(lower=0) / 8.0) * cad
    n = _steps(72, cad)
    out["T15"] = excess.rolling(n, min_periods=n).sum()          # °C·h over 72 h

    # -- compound (proxies)
    out["T16"] = ((-S("S23") / 6.0).clip(lower=0) + S("S24").abs() / 45.0 + P("P18") / 20.0
                  + ((1010.0 - P("P67")) / 25.0).clip(lower=0))
    out["T17"] = -S("S39") + out["T3"] / 400.0 - S("S8") / PROFILE_DEPTH_MM * 2
    return out


def _sigmoid(x):
    return 1.0 / (1.0 + np.exp(-x))


def _cascade_loop(inputs: list[np.ndarray], init: float, step, cad_h: float) -> np.ndarray:
    """Run a stateful daily-code recursion hourly.  Any NaN input resets the
    state to ``init`` and masks the output for CASCADE_SPINUP_H hours."""
    n = len(inputs[0])
    out = np.full(n, np.nan)
    state = init
    spin = _steps(CASCADE_SPINUP_H, cad_h)
    warm = 0
    for i in range(n):
        vals = [a[i] for a in inputs]
        if any(not np.isfinite(v) for v in vals):
            state, warm = init, 0
            continue
        state = step(state, *vals)
        warm += 1
        if warm > spin:
            out[i] = state
    return out


def _ffmc(vpd, rain_rate, t, rh, ws, cad_h) -> pd.Series:
    """Fine fuel moisture code — hourly proxy of Van Wagner (1987).

    Uses the published EMC (Ed/Ew), rain-wetting and drying-rate forms,
    applied per step with the daily rate scaled by cad/24, plus a VPD
    multiplier on the drying rate (the spec lists S13 as an input; in CFFDRS
    VPD is implicit in T/RH).  Not the reference hourly FFMC.
    """
    def step(F, vpd_kpa, r_mm_h, T, H, W):
        m = 147.2 * (101.0 - F) / (59.5 + F)
        r = r_mm_h * cad_h
        if r > 0.5:
            rf = r - 0.5
            m += 42.5 * rf * math.exp(-100.0 / (251.0 - m)) * (1.0 - math.exp(-6.93 / rf))
            if m > 150.0:
                m += 0.0015 * (m - 150.0) ** 2 * math.sqrt(rf)
            m = min(m, 250.0)
        Ed = 0.942 * H ** 0.679 + 11.0 * math.exp((H - 100.0) / 10.0) + 0.18 * (21.1 - T) * (1.0 - math.exp(-0.115 * H))
        Wk = W * 3.6
        if m > Ed:
            k = (0.424 * (1 - (H / 100.0) ** 1.7) + 0.0694 * math.sqrt(Wk) * (1 - (H / 100.0) ** 8)) * 0.581 * math.exp(0.0365 * T)
            k *= (1.0 + 0.05 * max(vpd_kpa, 0.0))
            m = Ed + (m - Ed) * 10.0 ** (-k * cad_h / 24.0)
        else:
            Ew = 0.618 * H ** 0.753 + 10.0 * math.exp((H - 100.0) / 10.0) + 0.18 * (21.1 - T) * (1.0 - math.exp(-0.115 * H))
            if m < Ew:
                k = (0.424 * (1 - ((100.0 - H) / 100.0) ** 1.7) + 0.0694 * math.sqrt(Wk) * (1 - ((100.0 - H) / 100.0) ** 8)) * 0.581 * math.exp(0.0365 * T)
                m = Ew - (Ew - m) * 10.0 ** (-k * cad_h / 24.0)
        return max(0.0, min(101.0, 59.5 * (250.0 - m) / (147.2 + m)))

    arrs = [s.to_numpy(dtype=float) for s in (vpd, rain_rate, t, rh, ws)]
    return _wrap(_cascade_loop(arrs, FFMC_INIT, step, cad_h), vpd)


def _dmc(api7, t, rh, cad_h) -> pd.Series:
    """Duff moisture code — proxy.  Drying per Van Wagner with a fixed 12 h
    day-length factor; wetting driven by positive steps of S2 (new rain),
    since the spec derives T2 from the 7-day API rather than raw rain."""
    d_api = api7.diff().clip(lower=0)

    def step(D, dA, T, H):
        if dA > 1.5:
            re = 0.92 * dA - 1.27
            M = 20.0 + math.exp(5.6348 - D / 43.43)
            b = 100.0 / (0.5 + 0.3 * D) if D <= 33 else (14.0 - 1.3 * math.log(D) if D <= 65 else 6.2 * math.log(D) - 17.2)
            Mr = M + 1000.0 * re / (48.77 + b * re)
            D = max(0.0, 43.43 * (5.6348 - math.log(Mr - 20.0)))
        K = 1.894 * (max(T, -1.1) + 1.1) * (100.0 - H) * 12.0 * 1e-6
        return D + 100.0 * K * cad_h / 24.0

    arrs = [s.to_numpy(dtype=float) for s in (d_api, t, rh)]
    return _wrap(_cascade_loop(arrs, DMC_INIT, step, cad_h), api7)


def _dc(api30, t, cad_h) -> pd.Series:
    """Drought code — proxy, same construction as _dmc with S3 driving wetting."""
    d_api = api30.diff().clip(lower=0)

    def step(D, dA, T):
        if dA > 2.8:
            rd = 0.83 * dA - 1.27
            Q = 800.0 * math.exp(-D / 400.0)
            Qr = Q + 3.937 * rd
            D = max(0.0, 400.0 * math.log(800.0 / Qr))
        V = max(0.0, 0.36 * (max(T, -2.8) + 2.8) + 1.4)
        return D + 0.5 * V * cad_h / 24.0

    arrs = [s.to_numpy(dtype=float) for s in (d_api, t)]
    return _wrap(_cascade_loop(arrs, DC_INIT, step, cad_h), api30)


def _isi(ffmc, u, v) -> pd.Series:
    """Initial spread index — published FWI-system equation."""
    m = 147.2 * (101.0 - ffmc) / (59.5 + ffmc)
    wk = 3.6 * np.sqrt(u * u + v * v)
    fW = np.exp(0.05039 * wk)
    fF = 91.9 * np.exp(-0.1386 * m) * (1.0 + m ** 5.31 / 4.93e7)
    return 0.208 * fW * fF


def _bui(dmc, dc) -> pd.Series:
    """Buildup index — published FWI-system equation."""
    den = dmc + 0.4 * dc
    low = 0.8 * dmc * dc / den.where(den > 0)
    high = dmc - (1.0 - 0.8 * dc / den.where(den > 0)) * (0.92 + (0.0114 * dmc) ** 1.7)
    return low.where(dmc <= 0.4 * dc, high).clip(lower=0)


def _fwi(isi, bui) -> pd.Series:
    """Fire weather index — published FWI-system equation."""
    fD = (0.626 * bui ** 0.809 + 2.0).where(bui <= 80.0, 1000.0 / (25.0 + 108.64 * np.exp(-0.023 * bui)))
    B = 0.1 * isi * fD
    S = np.exp(2.72 * (0.434 * np.log(B.where(B > 0))) ** 0.647)
    return S.where(B > 1.0, B)


def _spread_rate(ffmc, slope_deg, gust, fuel_class) -> pd.Series:
    """Fire spread rate estimate — proxy (m/min-ish, relative).
    fF(FFMC)/100 × fuel factor × gust factor × Rothermel-style slope factor."""
    m = 147.2 * (101.0 - ffmc) / (59.5 + ffmc)
    fF = 91.9 * np.exp(-0.1386 * m) * (1.0 + m ** 5.31 / 4.93e7)
    fuel = fuel_class.map(lambda c: FUEL_SPREAD_FACTOR.get(int(c), np.nan) if np.isfinite(c) else np.nan).astype(float)
    slope_f = np.exp(3.533 * np.tan(np.deg2rad(slope_deg.clip(lower=0))) ** 1.2)
    return fF / 100.0 * fuel * gust.clip(lower=1.0) * slope_f


def _fos(slope_deg, pore_kpa, deficit_mm, c_kpa=5.0, phi_deg=30.0, gamma=18.0, z_m=1.5) -> pd.Series:
    """Infinite-slope factor of safety — proxy with fixed soil constants;
    apparent cohesion decays as the saturation deficit closes."""
    b = np.deg2rad(slope_deg.clip(lower=1.0))
    c_eff = c_kpa * deficit_mm / (deficit_mm + 50.0)
    num = c_eff + (gamma * z_m * np.cos(b) ** 2 - pore_kpa.clip(lower=0)) * math.tan(math.radians(phi_deg))
    den = gamma * z_m * np.sin(b) * np.cos(b)
    return (num / den).clip(lower=0, upper=10)


# ------------------------------------------------------------------ top level
def compute_features(prim: pd.DataFrame, static: dict[str, float] | None = None,
                     min_coverage: float = 1.0) -> pd.DataFrame:
    """All 39 secondary + 17 tertiary parameters (+ S23_24h) on ``prim.index``."""
    sec = compute_secondary(prim, static, min_coverage)
    ter = compute_tertiary(prim, sec, static)
    out = pd.concat([sec, ter], axis=1)
    expected = SECONDARY_IDS + EXTRA_COLUMNS + TERTIARY_IDS
    missing = set(expected) - set(out.columns)
    if missing:
        raise RuntimeError(f"features not produced: {sorted(missing)}")
    return out[[c for c in out.columns]]


def assemble_trained_channels(prim: pd.DataFrame, feats: pd.DataFrame,
                              static: dict[str, float] | None = None) -> tuple[pd.DataFrame, pd.DataFrame]:
    """(values, mask) DataFrames with exactly the 41 columns of
    ``registry.trained_channels()`` in that order.  Values are NaN→0 *only*
    after the mask has been taken, so no fabricated value ever reaches the
    model without a 0 in the parallel mask."""
    static = dict(static or {})
    cols = registry.trained_channels()
    vals = pd.DataFrame(index=prim.index, columns=cols, dtype=float)
    for c in cols:
        if c in prim.columns:
            vals[c] = prim[c].astype(float)
        elif c in feats.columns:
            vals[c] = feats[c].astype(float)
        elif c in static:
            vals[c] = float(static[c])
        elif c in ("P77_lat", "P77_lon"):
            vals[c] = np.nan
        else:
            vals[c] = np.nan
    mask = vals.notna().astype(np.float32)
    vals = vals.fillna(0.0).astype(np.float32)
    return vals, mask
