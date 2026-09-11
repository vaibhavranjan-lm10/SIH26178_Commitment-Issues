"""Synthetic multi-column corpus generator.

    ╔════════════════════════════════════════════════════════════════════╗
    ║  PLACEHOLDER DATA.  This is NOT an ERA5 / IMERG / SMAP / GloFAS /  ║
    ║  MODIS / Sentinel pipeline and produces NO real observations.      ║
    ║  It exists only because the five open data-pipeline decisions       ║
    ║  (CLAUDE.md; parameters doc "Open decisions") are unresolved and    ║
    ║  there is therefore no real training sample yet.  Every number it   ║
    ║  emits is a made-up but physically-ranged stand-in so the feature   ║
    ║  code, model, training loop, calibration and ONNX export can be     ║
    ║  built and exercised end to end.  Metrics obtained on this corpus   ║
    ║  say nothing about real-world skill.                                ║
    ╚════════════════════════════════════════════════════════════════════╝

What it does produce, so the rest of the pipeline has the right *shape*:

* N columns (blueprint §5: one column = one site = one graph vertex) laid
  out in a ~12 km domain with a DEM, positions, profiles (§5.3: riverine /
  forest / urban / industrial / village) and per-profile transducer
  population — unpopulated channels are simply absent (→ masked).
* The 82 primaries per column on a uniform hourly grid (the hourly grid
  is an *assumption* standing in for open decision 2, the resampling rule).
* Random pod drop-outs (§2 failure table: "a pod → those channels masked").
* Injected hazard events for the seven trained heads with primary-channel
  signatures and *spatial propagation* over the column graph (flood
  travels downstream, fire upslope/downwind, pollution and heat are
  regional, cyclones sweep the whole domain), so the graph stage has
  something to learn.  GL and WQ get no events (declared-only heads).
* Labels per head and horizon (heads.py): t+0 = event active now,
  t+Δ = an event of that hazard is active at some point in (t, t+Δ].

The P75 land-cover codes are the table in features.LAND_COVER_CODES.
"""
from __future__ import annotations

import math
from dataclasses import dataclass, field

import numpy as np
import pandas as pd

from . import features as F
from .heads import HEADS

PROFILES = ("riverine", "forest", "urban", "industrial", "village")

# Which in-situ primaries each column profile populates (blueprint §5.3 pod
# population, §11.4 village example).  Satellite/reanalysis P47–P69 are
# delivered to every column over the uplink; static P70–P82 are per site.
_SOIL3 = ["P1", "P2", "P3", "P4", "P5", "P6", "P7", "P8", "P9"]
_SOIL1 = ["P1", "P4", "P7"]
_G = ["P10", "P12", "P42", "P43", "P44", "P32", "P45", "P46"]
_U = ["P13", "P15", "P17", "P22", "P23", "P21", "P24", "P30", "P31", "P33"]
_C = ["P14", "P16", "P18", "P19", "P20"]
_AQ_FULL = ["P25", "P26", "P27", "P28", "P29"]
_GAS = ["P34", "P35", "P36"]
_WQ = ["P37", "P38", "P39", "P40", "P41"]
PROFILE_CHANNELS: dict[str, list[str]] = {
    "riverine": _SOIL3 + _G + _U + ["P18", "P19", "P20", "P11", "P38"],
    "forest": _SOIL3 + _G + _U + _C + ["P12"],
    "urban": _G + _U + _C + _AQ_FULL + ["P11"],
    "industrial": _G + _U + _AQ_FULL + _GAS + _WQ + ["P11"],
    "village": _SOIL1 + _G + _U + ["P18", "P19", "P11", "P38"],
}
# Pods that can drop out together (§2: "a pod → those channels masked")
POD_GROUPS = {"S": _SOIL3, "G": _G, "U": _U, "C": _C, "R": ["P11", "P38"]}


@dataclass
class Site:
    cid: str
    profile: str
    x_m: float
    y_m: float
    elev_m: float
    static: dict[str, float]
    dem: np.ndarray
    channels: list[str]


@dataclass
class Event:
    hazard: str
    cid: str
    start: int          # row index
    end: int            # exclusive
    intensity: float


@dataclass
class Column:
    site: Site
    primaries: pd.DataFrame      # hourly, columns = populated P-ids (+ satellite)
    labels: pd.DataFrame         # columns = heads.OUTPUT_NAMES (GL/WQ all zero)
    events: list[Event]


@dataclass
class Corpus:
    columns: list[Column]
    edges: list[tuple[int, int, float]]   # (i, j, weight) undirected, i<j
    index: pd.DatetimeIndex
    seed: int
    notes: str = "SYNTHETIC PLACEHOLDER CORPUS — not real observations (see synthetic.py)"

    def adjacency(self) -> np.ndarray:
        n = len(self.columns)
        A = np.zeros((n, n))
        for i, j, w in self.edges:
            A[i, j] = A[j, i] = w
        return A


# ------------------------------------------------------------------ sites
def _make_sites(n: int, rng: np.random.Generator, domain_m: float = 12000.0) -> list[Site]:
    # A regional DEM: a valley running roughly W→E along y = domain/2, hills to the north.
    def regional_elev(x, y):
        return 300 + 0.03 * abs(y - domain_m * 0.5) + 25 * math.sin(x / 1500.0) + 0.004 * (domain_m - x)

    sites = []
    profiles = [PROFILES[i % len(PROFILES)] for i in range(n)]
    rng.shuffle(profiles)
    for i in range(n):
        x = rng.uniform(500, domain_m - 500)
        # riverine / urban near the valley floor, forest on the hill, others anywhere
        if profiles[i] in ("riverine", "urban"):
            y = domain_m * 0.5 + rng.normal(0, 800)
        elif profiles[i] == "forest":
            y = domain_m * (0.5 + rng.choice([-1, 1]) * rng.uniform(0.25, 0.45))
        else:
            y = rng.uniform(500, domain_m - 500)
        cell = 30.0
        gg = np.arange(-7, 8) * cell
        dem = np.array([[regional_elev(x + dx, y + dy) + rng.normal(0, 0.3) for dx in gg] for dy in gg])
        terr = F.terrain_from_dem(dem, cell, channel_threshold_cells=15)
        lc = {"riverine": 40, "forest": 10, "urban": 50, "industrial": 50, "village": 40}[profiles[i]]
        sand = rng.uniform(20, 70)
        clay = rng.uniform(10, min(40, 90 - sand))
        static = {
            "P70": float(dem[7, 7]), "P71": sand, "P72": 100 - sand - clay, "P73": clay,
            "P74": rng.uniform(1.2, 1.6), "P75": lc, "P76": {10: rng.uniform(10, 25)}.get(lc, rng.uniform(0, 5)),
            "P77_lat": 23.0 + y / 111000.0, "P77_lon": 85.0 + x / 102000.0,
            "P78": rng.uniform(2000, 30000), "P79": int(rng.integers(1, 6)), "P80": rng.uniform(50, 3000),
            "P81": rng.uniform(0, 0.2), "P82": {50: rng.uniform(0.5, 0.9)}.get(lc, rng.uniform(0.0, 0.1)),
            **terr.as_dict(),
        }
        sites.append(Site(f"C{i:02d}", profiles[i], x, y, static["P70"], static, dem, PROFILE_CHANNELS[profiles[i]]))
    return sites


def _edges(sites: list[Site], radius_m: float = 4500.0) -> list[tuple[int, int, float]]:
    """§7.4 adjacency prior: distance-weighted, within LoRa reach."""
    out = []
    for i in range(len(sites)):
        for j in range(i + 1, len(sites)):
            d = math.hypot(sites[i].x_m - sites[j].x_m, sites[i].y_m - sites[j].y_m)
            if d < radius_m:
                out.append((i, j, float(math.exp(-d / 3000.0))))
    return out


# ------------------------------------------------------------------ base weather
def _base_weather(n: int, rng: np.random.Generator, site: Site, t0_hour: int = 0) -> dict[str, np.ndarray]:
    t = np.arange(n)
    hod = (t + t0_hour) % 24
    diurnal = -np.cos(2 * np.pi * (hod - 3) / 24)           # min at 03:00, max at 15:00
    season = 2.0 * np.sin(2 * np.pi * t / (24 * 365) + 1.0)
    synoptic = np.cumsum(rng.normal(0, 0.15, n)); synoptic -= np.convolve(synoptic, np.ones(240) / 240, "same")
    T = 27 + season + 5.5 * diurnal + synoptic + rng.normal(0, 0.4, n) - 0.0065 * (site.elev_m - 300)
    RH = np.clip(65 - 22 * diurnal - 2 * synoptic + rng.normal(0, 3, n), 8, 100)
    P = 1008 - 0.11 * (site.elev_m - 300) + 1.2 * np.sin(2 * np.pi * hod / 12) + np.cumsum(rng.normal(0, 0.08, n)) * 0.3
    P -= np.convolve(P - P.mean(), np.ones(480) / 480, "same") * 0.5
    ws_ar = np.zeros(n); wd = np.zeros(n)
    ws_ar[0] = 2.0; wd[0] = 220.0
    for i in range(1, n):
        ws_ar[i] = max(0.05, 0.9 * ws_ar[i - 1] + rng.normal(0.25, 0.45))
        wd[i] = (wd[i - 1] + rng.normal(0, 8)) % 360
    ws = ws_ar * (1 + 0.3 * diurnal.clip(0))
    solar = np.clip(900 * np.sin(np.pi * (hod - 6) / 12), 0, None) * (hod >= 6) * (hod <= 18)
    pbl = 300 + 1200 * np.clip(diurnal, 0, None) + rng.normal(0, 40, n)
    # rain: clustered storms (monsoon-ish)
    rain = np.zeros(n)
    i = 0
    while i < n:
        if rng.random() < 0.012:
            dur = int(rng.integers(2, 14)); amp = rng.exponential(2.0)
            _add(rain, i, amp * rng.random(dur))
            i += dur
        i += 1
    return {"T": T, "RH": RH, "P": P, "ws": ws, "wd": wd, "solar": solar, "pbl": pbl, "rain": rain,
            "diurnal": diurnal}


# ------------------------------------------------------------------ events
def _inject_events(sites: list[Site], edges, n: int, rng: np.random.Generator,
                   rates: dict[str, float]) -> dict[int, list[Event]]:
    """Return {column index: events}.  Rates are expected events per 100 days
    per *seed* column; propagation adds more at neighbours."""
    N = len(sites)
    nbrs = {i: [] for i in range(N)}
    for i, j, w in edges:
        nbrs[i].append((j, w)); nbrs[j].append((i, w))
    ev: dict[int, list[Event]] = {i: [] for i in range(N)}
    days = n / 24.0
    warm = 35 * 24          # leave the feature warm-up period event-free

    def n_events(rate):
        return rng.poisson(rate * days / 100.0)

    def start(lo, hi):
        """Random start in [lo, hi), or None when the corpus is too short for this event."""
        return int(rng.integers(lo, hi)) if hi > lo else None

    # FL: seed at the most upstream (highest) riverine/village columns, propagate downstream (lower elev) with lag
    valley = [k for k in range(N) if sites[k].profile in ("riverine", "village", "urban")] or list(range(N))
    order = sorted(valley, key=lambda k: -sites[k].elev_m)     # most upstream valley columns seed floods
    for _ in range(n_events(rates["FL"])):
        seed = order[int(rng.integers(0, max(1, len(order) // 2)))]
        s = start(warm, n - 96)
        if s is None:
            continue
        dur = int(rng.integers(18, 48)); inten = rng.uniform(0.6, 1.0)
        frontier = [(seed, s)]; seen = {seed}
        while frontier:
            k, st = frontier.pop(0)
            ev[k].append(Event("FL", sites[k].cid, st, min(n, st + dur), inten))
            for j, w in nbrs[k]:
                if j not in seen and sites[j].elev_m < sites[k].elev_m + 5:
                    seen.add(j); frontier.append((j, st + int(rng.integers(3, 10))))
    # UF: short intense burst; urban/village seeds, 1-hop spill with lag
    for _ in range(n_events(rates["UF"])):
        cands = [k for k in range(N) if sites[k].profile in ("urban", "village", "industrial")] or list(range(N))
        k = int(rng.choice(cands)); s = start(warm, n - 24)
        if s is None:
            continue
        dur = int(rng.integers(3, 8))
        ev[k].append(Event("UF", sites[k].cid, s, s + dur, rng.uniform(0.6, 1.0)))
        for j, w in nbrs[k]:
            if rng.random() < 0.5 * w + 0.2:
                ev[j].append(Event("UF", sites[j].cid, s + int(rng.integers(1, 3)), s + dur + 1, rng.uniform(0.4, 0.8)))
    # FI: dry spell then ignition at a forest column, spreads upslope / to neighbours with lag
    for _ in range(n_events(rates["FI"])):
        cands = [k for k in range(N) if sites[k].profile == "forest"] or list(range(N))
        k = int(rng.choice(cands)); s = start(warm + 120, n - 72)
        if s is None:
            continue
        dur = int(rng.integers(12, 48))
        ev[k].append(Event("FI", sites[k].cid, s, s + dur, rng.uniform(0.6, 1.0)))
        for j, w in nbrs[k]:
            if sites[j].elev_m >= sites[k].elev_m - 20 and rng.random() < 0.6:
                lag = int(rng.integers(6, 24))
                ev[j].append(Event("FI", sites[j].cid, s + lag, s + lag + dur, rng.uniform(0.4, 0.9)))
    # PO: regional stagnation — every column in the connected cluster, small offsets
    for _ in range(n_events(rates["PO"])):
        s = start(warm, n - 96)
        if s is None:
            continue
        dur = int(rng.integers(36, 96))
        for k in range(N):
            off = int(rng.integers(0, 6))
            ev[k].append(Event("PO", sites[k].cid, s + off, s + off + dur, rng.uniform(0.5, 1.0) * (1.2 if sites[k].profile in ("urban", "industrial") else 0.8)))
    # LS: local, at the steepest columns, after a wet spell
    steep = sorted(range(N), key=lambda k: -sites[k].static["S30"])[: max(2, N // 3)]
    for _ in range(n_events(rates["LS"])):
        k = int(rng.choice(steep)); s = start(warm + 48, n - 24)
        if s is None:
            continue
        dur = int(rng.integers(6, 14))
        ev[k].append(Event("LS", sites[k].cid, s, s + dur, rng.uniform(0.6, 1.0)))
    # HW: regional multi-day anomaly
    for _ in range(n_events(rates["HW"])):
        s = start(warm, n - 144)
        if s is None:
            continue
        dur = int(rng.integers(72, 144)); inten = rng.uniform(0.6, 1.0)
        for k in range(N):
            ev[k].append(Event("HW", sites[k].cid, s, s + dur, inten))
    # CY: regional, rare; pressure fall precedes, peak winds for ~30 h
    for _ in range(n_events(rates["CY"])):
        s = start(warm + 48, n - 60)
        if s is None:
            continue
        dur = int(rng.integers(24, 36)); inten = rng.uniform(0.7, 1.0)
        for k in range(N):
            ev[k].append(Event("CY", sites[k].cid, s + int(sites[k].x_m / 6000.0), s + int(sites[k].x_m / 6000.0) + dur, inten))
    return ev


def _add(arr: np.ndarray, start: int, vals: np.ndarray) -> None:
    """arr[start:start+len(vals)] += vals, clipped to the array bounds."""
    n = len(arr)
    a, b = max(0, start), min(n, start + len(vals))
    if b > a:
        arr[a:b] += vals[a - start: b - start]


def _ramp(n, start, end, pre=0, post=0):
    """Envelope 0→1→0: linear rise over `pre` h before start, hold, decay over `post` h after end."""
    e = np.zeros(n); t = np.arange(n)
    if pre > 0:
        m = (t >= start - pre) & (t < start); e[m] = (t[m] - (start - pre)) / pre
    e[(t >= start) & (t < end)] = 1.0
    if post > 0:
        m = (t >= end) & (t < end + post); e[m] = np.exp(-(t[m] - end) / (post / 3.0))
    return e


# ------------------------------------------------------------------ one column
def _column_series(site: Site, n: int, rng: np.random.Generator, events: list[Event],
                   upstream_rain: np.ndarray | None) -> pd.DataFrame:
    w = _base_weather(n, rng, site)
    T, RH, P, ws, wd, rain = w["T"], w["RH"], w["P"].copy(), w["ws"], w["wd"], w["rain"].copy()
    smoke = np.abs(rng.normal(0.05, 0.02, n)); pm25 = np.abs(rng.normal(30, 8, n)); frp = np.zeros(n)
    tilt_drift = np.zeros(n); pore_extra = np.zeros(n); skin_extra = np.zeros(n)
    stage_extra = np.zeros(n); pbl = w["pbl"].copy(); wd_extra = np.zeros(n); ws_mult = np.ones(n)
    T_extra = np.zeros(n); discharge_extra = np.zeros(n)

    for e in events:
        s, d, k = e.start, e.end, e.intensity
        if e.hazard == "FL":
            env = _ramp(n, s, d, pre=6, post=36)
            _add(rain, s - 12, k * rng.uniform(2, 6, 18))
            stage_extra += 2.5 * k * env; discharge_extra += 300 * k * env
        elif e.hazard == "UF":
            _add(rain, s, k * rng.uniform(8, 20, d - s))
            stage_extra += 1.2 * k * _ramp(n, s, d, post=4)
        elif e.hazard == "FI":
            dry = _ramp(n, s - 120, s, pre=24)
            RH -= 25 * k * dry; T_extra += 4 * k * dry; ws_mult += 0.6 * k * dry
            env = _ramp(n, s, d, post=12)
            smoke += 0.9 * k * env; pm25 += 150 * k * env; frp += 40 * k * env; skin_extra += 25 * k * env
        elif e.hazard == "PO":
            env = _ramp(n, s, d, pre=12, post=12)
            pbl *= (1 - 0.6 * k * env); ws_mult *= (1 - 0.5 * k * env); pm25 += 120 * k * env
        elif e.hazard == "LS":
            wet = _ramp(n, s - 60, s, pre=12)
            _add(rain, s - 60, k * rng.uniform(1, 4, 60))
            pore_extra += 30 * k * wet; tilt_drift += np.cumsum(12 * k * wet) / 24.0
            tilt_drift += 40 * k * _ramp(n, s, d)
        elif e.hazard == "HW":
            env = _ramp(n, s, d, pre=24, post=24)
            T_extra += 7.5 * k * env; RH -= 12 * k * env
        elif e.hazard == "CY":
            fall = _ramp(n, s - 48, s, pre=36)
            P -= 22 * k * fall; P -= 6 * k * _ramp(n, s, d, post=24)
            env = _ramp(n, s - 12, d, pre=24, post=24)
            ws_mult += 5.0 * k * env; rain += 12 * k * env * rng.random(n)
            wd_extra += np.cumsum(env) * 4.0    # steady rotation through the passage

    RH = np.clip(RH, 5, 100); T = T + T_extra; ws = ws * ws_mult; wd = (wd + wd_extra) % 360
    rain = np.clip(rain, 0, None)
    # soil moisture: three layers, rain infiltrates with drainage; deeper = slower
    theta = {}
    prev = np.array([0.22, 0.26, 0.30]); tau = np.array([36.0, 96.0, 240.0]); gain = np.array([0.012, 0.006, 0.003])
    th = np.zeros((n, 3))
    for i in range(n):
        inflow = rain[i]
        prev = prev + gain * inflow - (prev - np.array([0.12, 0.16, 0.20])) / tau
        prev[1] += 0.002 * max(prev[0] - prev[1], 0); prev[2] += 0.001 * max(prev[1] - prev[2], 0)
        prev = np.clip(prev, 0.05, 0.48); th[i] = prev
    theta["P1"], theta["P2"], theta["P3"] = th[:, 0], th[:, 1], th[:, 2]
    # stage: local rain routing + upstream contribution + injected flood
    kern = np.exp(-np.arange(72) / 18.0); kern /= kern.sum()
    stage = 0.8 + 0.15 * np.convolve(rain, kern, "full")[:n]
    if upstream_rain is not None:
        stage += 0.10 * np.convolve(upstream_rain, np.exp(-np.arange(96) / 30.0) / 30.0, "full")[:n]
    stage += stage_extra + rng.normal(0, 0.02, n)

    df = pd.DataFrame(index=pd.RangeIndex(n))
    df["P1"], df["P2"], df["P3"] = theta["P1"] + rng.normal(0, 0.003, n), theta["P2"] + rng.normal(0, 0.003, n), theta["P3"] + rng.normal(0, 0.003, n)
    df["P4"], df["P5"], df["P6"] = T - 2 + 0.5 * w["diurnal"], T.mean() - 1.5 + rng.normal(0, 0.1, n), T.mean() - 2.5 + rng.normal(0, 0.05, n)
    df["P7"], df["P8"], df["P9"] = 0.2 + 0.3 * theta["P1"], 0.2 + 0.3 * theta["P2"], 0.2 + 0.3 * theta["P3"]
    df["P10"] = np.clip(theta["P1"] + 0.03 * (rain > 0) + rng.normal(0, 0.005, n), 0.03, 0.5)
    df["P11"] = stage
    df["P12"] = np.cumsum(rain)                          # cumulative counter
    df["P13"], df["P14"] = T, T - 1.2 + rng.normal(0, 0.3, n)
    df["P15"], df["P16"] = RH, np.clip(RH - 6, 3, 100)
    df["P17"] = P
    df["P18"], df["P19"] = ws, wd
    df["P20"] = w["solar"] * (1 - 0.7 * (rain > 0.5))
    df["P22"] = pm25; df["P23"] = pm25 * rng.uniform(1.5, 2.2, n); df["P21"] = pm25 * 0.7
    df["P24"] = 0.5 + 0.02 * pm25 + 3 * smoke; df["P25"] = 10 + 0.3 * pm25; df["P26"] = 20 + 15 * np.clip(w["diurnal"], 0, None)
    df["P27"], df["P28"], df["P29"] = 3 + 0.1 * pm25, 15 + rng.normal(0, 2, n), 1 + 0.02 * pm25
    df["P30"] = 420 + 30 * (1 - np.clip(w["diurnal"], 0, None)) + 80 * smoke; df["P31"] = 50 + 100 * smoke
    df["P32"] = T + 4 + 8 * np.clip(w["diurnal"], 0, None) + skin_extra
    df["P33"] = smoke
    df["P34"], df["P35"], df["P36"] = 2 + rng.normal(0, 0.2, n), 1 + rng.normal(0, 0.2, n), 0.1 + rng.normal(0, 0.02, n)
    df["P37"], df["P38"] = 7.3 + rng.normal(0, 0.05, n), 8 + 40 * (stage - 0.8).clip(0) + rng.normal(0, 1, n)
    df["P39"], df["P40"], df["P41"] = 8 - 0.1 * (T - 25), 320 + rng.normal(0, 10, n), T - 2
    df["P42"], df["P43"] = rng.normal(0, 0.8, n) + tilt_drift * 0.6, rng.normal(0, 0.8, n) + tilt_drift * 0.8
    df["P44"] = 1000 + rng.normal(0, 0.8, n) - tilt_drift * 0.05
    df["P45"] = 5 + 60 * (theta["P2"] - 0.16) + pore_extra
    df["P46"] = 30 * w["diurnal"]
    # satellite / reanalysis (coarser cadences, held constant between updates — no interpolation)
    df["P47"] = rain * rng.uniform(0.7, 1.3, n)
    df["P48"] = np.roll(rain, -3) * rng.uniform(0.5, 1.5, n)
    df["P49"] = _hold(theta["P1"] + rng.normal(0, 0.02, n), 72)
    df["P50"] = _hold(theta["P2"], 3)
    df["P51"] = _hold(40 + 200 * (stage - 0.8).clip(0) + discharge_extra, 24)
    df["P52"] = _hold(T + 6 + skin_extra * 0.5, 24); df["P53"] = _hold(T - 4, 24)
    df["P54"] = _hold(frp, 12); df["P55"] = _hold(np.cumsum(frp) / 5000.0, 720)
    ndvi_base = {10: 0.7, 20: 0.45, 30: 0.4, 40: 0.5, 50: 0.15, 60: 0.1, 80: -0.2, 90: 0.35}[int(site.static["P75"])]
    red = _hold(0.08 + 0.04 * (1 - ndvi_base) + rng.normal(0, 0.005, n), 120)
    df["P56"] = red; df["P57"] = _hold(red * (1 + ndvi_base) / (1 - ndvi_base) + rng.normal(0, 0.01, n), 120)
    df["P58"] = _hold(0.2 - 0.15 * theta["P1"] + rng.normal(0, 0.01, n), 120); df["P59"] = _hold(0.09 + rng.normal(0, 0.005, n), 120)
    df["P60"] = _hold(-11 - 12 * (stage - 0.8).clip(0) + rng.normal(0, 0.5, n), 168)
    df["P61"] = _hold(-19 - 12 * (stage - 0.8).clip(0) + rng.normal(0, 0.5, n), 168)
    df["P62"] = _hold(0.2 + 0.004 * pm25, 24)
    df["P63"], df["P64"] = T + rng.normal(0, 0.5, n), T - (100 - RH) / 5.0
    df["P65"], df["P66"] = -ws * np.sin(np.deg2rad(wd)) + rng.normal(0, 0.5, n), -ws * np.cos(np.deg2rad(wd)) + rng.normal(0, 0.5, n)
    df["P67"] = P + rng.normal(0, 0.2, n)
    df["P68"] = pbl
    df["P69"] = 0.0
    return df


def _hold(x: np.ndarray, every: int) -> np.ndarray:
    """Sample-and-hold at a coarser cadence (satellite products don't update hourly)."""
    idx = (np.arange(len(x)) // every) * every
    return x[idx]


def _labels(n: int, events: list[Event]) -> pd.DataFrame:
    active = {h.code: np.zeros(n, dtype=bool) for h in HEADS}
    for e in events:
        active[e.hazard][e.start: e.end] = True
    cols = {}
    for h in HEADS:
        a = active[h.code]
        for hz in h.horizons_h:
            if hz == 0:
                y = a.copy()
            else:
                # 1 if active at any point in (t, t+hz]
                cs = np.concatenate([[0], np.cumsum(a.astype(int))])
                t = np.arange(n); hi = np.minimum(t + hz + 1, n)
                y = (cs[hi] - cs[t + 1]) > 0
            cols[f"{h.code}_t{hz}"] = y.astype(np.float32)
    return pd.DataFrame(cols)


# expected events per 100 days per seed column (CY/PO/HW are regional: one event hits every column)
DEFAULT_RATES = {"FL": 2.5, "UF": 4.0, "FI": 2.5, "PO": 2.0, "LS": 3.0, "HW": 1.5, "CY": 1.0}


def generate_corpus(n_columns: int = 12, n_days: int = 120, seed: int = 0,
                    start: str = "2026-05-01", dropout_rate: float = 0.004,
                    rates: dict[str, float] | None = None) -> Corpus:
    """SYNTHETIC PLACEHOLDER — see module docstring.  Hourly cadence."""
    rng = np.random.default_rng(seed)
    n = n_days * 24
    sites = _make_sites(n_columns, rng)
    edges = _edges(sites)
    events = _inject_events(sites, edges, n, rng, {**DEFAULT_RATES, **(rates or {})})
    index = pd.date_range(start, periods=n, freq="1h")

    # upstream rain for stage routing: rain at higher-elevation neighbours
    frames: dict[int, pd.DataFrame] = {}
    order = sorted(range(len(sites)), key=lambda k: -sites[k].elev_m)
    nbr = {i: [] for i in range(len(sites))}
    for i, j, w in edges:
        nbr[i].append(j); nbr[j].append(i)
    for k in order:
        up = [frames[j]["P12"].diff().fillna(0).to_numpy() for j in nbr[k] if j in frames and sites[j].elev_m > sites[k].elev_m]
        up_rain = np.sum(up, axis=0) if up else None
        frames[k] = _column_series(sites[k], n, rng, events[k], up_rain)

    columns = []
    for k, site in enumerate(sites):
        df = frames[k]
        keep = [c for c in df.columns if c in site.channels or int(c[1:]) >= 47]
        df = df[keep].copy()
        # pod drop-outs: whole pod groups go silent for a few hours (masked, never filled)
        for pod, chans in POD_GROUPS.items():
            present = [c for c in chans if c in df.columns]
            if not present:
                continue
            i = 0
            while i < n:
                if rng.random() < dropout_rate:
                    d = int(rng.integers(1, 12)); df.loc[i: i + d - 1, present] = np.nan; i += d
                i += 1
        df.index = index
        lab = _labels(n, events[k]); lab.index = index
        columns.append(Column(site, df, lab, events[k]))
    return Corpus(columns, edges, index, seed)


def corpus_summary(c: Corpus) -> pd.DataFrame:
    rows = []
    for col in c.columns:
        r = {"cid": col.site.cid, "profile": col.site.profile, "n_channels": col.primaries.shape[1],
             "missing_frac": float(col.primaries.isna().mean().mean())}
        for h in HEADS:
            r[h.code] = sum(1 for e in col.events if e.hazard == h.code)
        rows.append(r)
    return pd.DataFrame(rows)
