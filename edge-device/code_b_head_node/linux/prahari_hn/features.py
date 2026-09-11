"""On-device computation of the 41 trained channels (stage 1 input).

This is the head-node implementation of the secondary/tertiary parameters
in the trained subset — S1, S2, S4, S6, S9, S13, S15, S16, S18, S30, S33,
S35, T6, T11 and the intermediates they need (S3, S11, S37, T1–T5) — over
the hourly grid the ring buffer serves.  It must agree numerically with
the offline reference (training_pipeline/prahari_train/features.py); the
test-suite checks that on the same data.  numpy only.

Missing-data policy: mask, never impute.  Every rolling operation needs its
whole window observed (a gap in P12 breaks S2 for the next 7 days, S3 for
30); the fire-weather cascade resets on any missing input and stays masked
through a 72-hour spin-up.  P12 is a cumulative counter (mm); all rain
consumers go through ``rain_increment``.
"""
from __future__ import annotations

import math

import numpy as np

MAGNUS_A, MAGNUS_B, MAGNUS_ES0 = 17.625, 243.04, 0.61094
LAYER_M = {"P1": 0.25, "P2": 0.45, "P3": 0.60}
PARTICLE_DENSITY = 2.65
API_DECAY = 0.90
SPINUP_H = 72
FFMC_INIT, DMC_INIT, DC_INIT = 85.0, 6.0, 15.0

# Primaries the trained channels need from the ring buffer (in situ + context)
PRIMARY_INPUTS = ["P1", "P2", "P3", "P10", "P11", "P12", "P13", "P14", "P15", "P17", "P18", "P19", "P20",
                  "P22", "P23", "P32", "P33", "P42", "P43", "P44",
                  "P47", "P49", "P51", "P52", "P54", "P57", "P58", "P60"]
STATIC_INPUTS = ["P70", "P71", "P73", "P74", "P75", "P77_lat", "P77_lon", "S30", "S33"]
HISTORY_HOURS = 30 * 24 + SPINUP_H     # extra history needed ahead of the context window


def rain_increment(p12: np.ndarray) -> np.ndarray:
    d = np.full_like(p12, np.nan)
    d[1:] = p12[1:] - p12[:-1]
    reset = d < 0
    d[reset] = p12[reset]
    return d


def _strict_roll(x: np.ndarray, n: int, fn) -> np.ndarray:
    """fn over each trailing window of n; NaN unless all n values are finite."""
    out = np.full_like(x, np.nan)
    if len(x) < n:
        return out
    win = np.lib.stride_tricks.sliding_window_view(x, n)
    ok = np.isfinite(win).all(axis=1)
    res = fn(win, axis=1)
    out[n - 1:] = np.where(ok, res, np.nan)
    return out


def decayed_sum(inc: np.ndarray, window_h: int) -> np.ndarray:
    n = window_h
    w = API_DECAY ** (np.arange(n) / 24.0)
    ok = np.isfinite(inc)
    conv = np.convolve(np.where(ok, inc, 0.0), w, mode="full")[: len(inc)]
    cnt = np.convolve(ok.astype(float), np.ones(n), mode="full")[: len(inc)]
    out = np.where(cnt >= n, conv, np.nan)
    out[: n - 1] = np.nan
    return out


def vpd(t, rh):
    rh = np.clip(rh, 0.1, 100.0)
    es = MAGNUS_ES0 * np.exp(MAGNUS_A * t / (t + MAGNUS_B))
    return es * (1.0 - rh / 100.0)


def theta_sat(sand, clay, bulk):
    phi = 1.0 - bulk / PARTICLE_DENSITY
    if np.isfinite(phi):
        return phi
    if np.isfinite(sand) and np.isfinite(clay):
        return 0.332 - 7.251e-4 * sand + 0.1276 * math.log10(max(clay, 0.1))
    return np.nan


def ksat_cosby(sand, clay):
    return 25.4 * 10.0 ** (-0.6 + 0.0126 * sand - 0.0064 * clay)


def _cascade(inputs, init, step):
    n = len(inputs[0])
    out = np.full(n, np.nan)
    state, warm = init, 0
    for i in range(n):
        vals = [a[i] for a in inputs]
        if any(not np.isfinite(v) for v in vals):
            state, warm = init, 0
            continue
        state = step(state, *vals)
        warm += 1
        if warm > SPINUP_H:
            out[i] = state
    return out


def ffmc(vpd_kpa, rain_rate, t, rh, ws):
    def step(F, v, r_mm_h, T, H, W):
        m = 147.2 * (101.0 - F) / (59.5 + F)
        r = r_mm_h
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
            k *= (1.0 + 0.05 * max(v, 0.0))
            m = Ed + (m - Ed) * 10.0 ** (-k / 24.0)
        else:
            Ew = 0.618 * H ** 0.753 + 10.0 * math.exp((H - 100.0) / 10.0) + 0.18 * (21.1 - T) * (1.0 - math.exp(-0.115 * H))
            if m < Ew:
                k = (0.424 * (1 - ((100.0 - H) / 100.0) ** 1.7) + 0.0694 * math.sqrt(Wk) * (1 - ((100.0 - H) / 100.0) ** 8)) * 0.581 * math.exp(0.0365 * T)
                m = Ew - (Ew - m) * 10.0 ** (-k / 24.0)
        return max(0.0, min(101.0, 59.5 * (250.0 - m) / (147.2 + m)))
    return _cascade([vpd_kpa, rain_rate, t, rh, ws], FFMC_INIT, step)


def dmc(d_api7, t, rh):
    def step(D, dA, T, H):
        if dA > 1.5:
            re = 0.92 * dA - 1.27
            M = 20.0 + math.exp(5.6348 - D / 43.43)
            b = 100.0 / (0.5 + 0.3 * D) if D <= 33 else (14.0 - 1.3 * math.log(D) if D <= 65 else 6.2 * math.log(D) - 17.2)
            Mr = M + 1000.0 * re / (48.77 + b * re)
            D = max(0.0, 43.43 * (5.6348 - math.log(Mr - 20.0)))
        K = 1.894 * (max(T, -1.1) + 1.1) * (100.0 - H) * 12.0 * 1e-6
        return D + 100.0 * K / 24.0
    return _cascade([d_api7, t, rh], DMC_INIT, step)


def dc(d_api30, t):
    def step(D, dA, T):
        if dA > 2.8:
            rd = 0.83 * dA - 1.27
            Q = 800.0 * math.exp(-D / 400.0)
            D = max(0.0, 400.0 * math.log(800.0 / (Q + 3.937 * rd)))
        V = max(0.0, 0.36 * (max(T, -2.8) + 2.8) + 1.4)
        return D + 0.5 * V / 24.0
    return _cascade([d_api30, t], DC_INIT, step)


def isi(ffmc_v, u, v):
    m = 147.2 * (101.0 - ffmc_v) / (59.5 + ffmc_v)
    wk = 3.6 * np.sqrt(u * u + v * v)
    return 0.208 * np.exp(0.05039 * wk) * 91.9 * np.exp(-0.1386 * m) * (1.0 + m ** 5.31 / 4.93e7)


def bui(dmc_v, dc_v):
    den = dmc_v + 0.4 * dc_v
    with np.errstate(invalid="ignore", divide="ignore"):
        den_ok = np.where(den > 0, den, np.nan)
        low = 0.8 * dmc_v * dc_v / den_ok
        high = dmc_v - (1.0 - 0.8 * dc_v / den_ok) * (0.92 + (0.0114 * dmc_v) ** 1.7)
    return np.clip(np.where(dmc_v <= 0.4 * dc_v, low, high), 0, None)


def fwi(isi_v, bui_v):
    with np.errstate(invalid="ignore", divide="ignore"):
        fD = np.where(bui_v <= 80.0, 0.626 * bui_v ** 0.809 + 2.0, 1000.0 / (25.0 + 108.64 * np.exp(-0.023 * bui_v)))
        B = 0.1 * isi_v * fD
        S = np.exp(2.72 * (0.434 * np.log(np.where(B > 0, B, np.nan))) ** 0.647)
    return np.where(B > 1.0, S, B)


def compute_trained_channels(prim: dict[str, np.ndarray], static: dict[str, float], channels: list[str],
                             n_out: int) -> tuple[np.ndarray, np.ndarray]:
    """prim: {P-id: hourly array (T,)} with NaN for missing, T ≥ n_out; static:
    per-site scalars.  Returns (values (n_out, C), mask (n_out, C)) for the
    last n_out hours in ``channels`` order, values 0 where masked."""
    T = len(next(iter(prim.values())))
    nan = np.full(T, np.nan)
    P = lambda k: np.asarray(prim.get(k, nan), dtype=np.float64)  # noqa: E731
    S = lambda k: float(static.get(k, np.nan))                     # noqa: E731
    f: dict[str, np.ndarray] = {}
    inc = rain_increment(P("P12"))
    f["S1"] = inc
    f["S2"] = decayed_sum(inc, 7 * 24)
    f["S3"] = decayed_sum(inc, 30 * 24)
    s4 = np.full(T, np.nan); s4[1:] = P("P11")[1:] - P("P11")[:-1]
    f["S4"] = s4
    f["S6"] = P("P1") - P("P2")
    ts = theta_sat(S("P71"), S("P73"), S("P74"))
    f["S9"] = 1000.0 * sum(np.clip(ts - P(k), 0, None) * LAYER_M[k] for k in ("P1", "P2", "P3"))
    s11 = ksat_cosby(S("P71"), S("P73"))
    f["S13"] = vpd(P("P13"), P("P15"))
    th = np.deg2rad(P("P19"))
    f["S15"] = -P("P18") * np.sin(th)
    f["S16"] = -P("P18") * np.cos(th)
    f["S18"] = P("P14") - P("P13")
    s30, s33 = S("S30"), S("S33")
    f["S30"] = np.full(T, s30)
    f["S33"] = np.full(T, s33)
    f["S35"] = np.full(T, math.log((s33 + 1.0) / math.tan(math.radians(max(s30, 0.1)))) if np.isfinite(s30) and np.isfinite(s33) else np.nan)
    mag = np.sqrt(P("P42") ** 2 + P("P43") ** 2 + P("P44") ** 2)
    lp = _strict_roll(mag, 6, np.median)
    s37 = np.full(T, np.nan); s37[24:] = lp[24:] - lp[:-24]
    t1 = ffmc(f["S13"], f["S1"], P("P13"), P("P15"), P("P18"))
    d7 = np.full(T, np.nan); d7[1:] = f["S2"][1:] - f["S2"][:-1]
    d30 = np.full(T, np.nan); d30[1:] = f["S3"][1:] - f["S3"][:-1]
    t2 = dmc(np.clip(d7, 0, None), P("P13"), P("P15"))
    t3 = dc(np.clip(d30, 0, None), P("P13"))
    f["T6"] = fwi(isi(t1, f["S15"], f["S16"]), bui(t2, t3))
    f["T11"] = (0.35 * np.clip(s30 / 45.0, 0, 1) + 0.30 * (1.0 - np.clip(f["S9"] / 200.0, 0, 1))
                + 0.15 * (1.0 - np.clip(math.log10(s11 + 1.0) / 2.5, 0, 1)) + 0.20 * np.clip(np.abs(s37) / 5.0, 0, 1))
    out = np.zeros((T, len(channels)), np.float32)
    for j, c in enumerate(channels):
        if c in prim:
            out[:, j] = P(c)
        elif c in f:
            out[:, j] = f[c]
        elif c in static:
            out[:, j] = S(c)
        else:
            out[:, j] = np.nan
    out = out[-n_out:]
    mask = np.isfinite(out).astype(np.float32)
    return np.where(mask > 0, out, 0.0).astype(np.float32), mask
