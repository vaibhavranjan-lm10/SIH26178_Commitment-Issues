"""Physical trajectories for the blueprint section 11 village worked example.

    "Pre-monsoon rain begins. The G pod's rain gauge counts. S1 shows soil
     moisture climbing toward saturation. The head node's antecedent
     precipitation index has been rising for four days. ... Two hours
     later, R1 reports river stage rising at 8 cm/hour. The landslide
     head, reading R3's tilt drift alongside pore pressure, crosses its
     own threshold."  -- blueprint section 11.7

Everything here is in SI physical units as a function of time; the
electrical mapping (probe transfer functions, register encodings, pulse
rates) is transducers.py. Nothing here is an application-level PRAHARI
parameter value ready to hand to a model -- it is the ground truth a
*transducer* would sense, which Code A must still acquire and process.

The trajectory is deterministic given a seed: a few pre-monsoon showers
over three days that each wet the soil and drain back partway, then a
sustained heavy burst on day four that drives the shallow soil to near
saturation, lifts pore-water pressure, starts the slope tilting, and
sends the river stage up at roughly 8 cm/h before it crests and recedes.
"""
from __future__ import annotations

import math
from dataclasses import dataclass, field

import numpy as np

HOUR = 3600.0
DAY = 24 * HOUR


@dataclass(frozen=True)
class RainBurst:
    start_h: float
    end_h: float
    total_mm: float
    label: str = ""


@dataclass
class VillageScenario:
    """Section 11 parameters. Ranges below are all physically plausible for a
    hill-foot village in the pre-monsoon; none is tuned to make a model fire,
    only to match the narrative in 11.7."""

    duration_h: float = 120.0                 # five days
    seed: int = 11                            # section 11

    # --- rain timeline (blueprint 11.7 "pre-monsoon rain ... four days") ---
    showers: tuple[RainBurst, ...] = (
        RainBurst(6, 10, 4.0, "day-1 shower"),
        RainBurst(30, 36, 7.0, "day-2 shower"),
        RainBurst(52, 60, 9.0, "day-3 shower"),
    )
    main_burst: RainBurst = RainBurst(84, 104, 42.0, "day-4 heavy burst")
    tip_mm: float = 0.2                       # tipping-bucket resolution (0.2 mm/tip)

    # --- soil (m3/m3), shallow probe at S1 (-10 cm) and surface at G (0-5 cm) ---
    vwc_dry_s1: float = 0.16
    vwc_dry_surface: float = 0.12
    vwc_saturation: float = 0.46
    # fraction of a burst's rain depth that reaches each layer, and its
    # drainage time constant back toward the dry baseline (hours)
    s1_infiltration: float = 0.024           # m3/m3 gained per mm at the burst peak
    s1_drain_tau_h: float = 60.0
    surface_infiltration: float = 0.05
    surface_drain_tau_h: float = 14.0

    # --- river stage at R1 (m above the dry-season bed) ---
    stage_base_m: float = 0.35
    stage_gain_m_per_mm: float = 0.020       # routed, lagged (section 11.7: ~8 cm/h on the rising limb)
    stage_lag_h: float = 4.0
    stage_recess_tau_h: float = 26.0

    # --- geotechnical at the slope toe (G in this overlay; R3 in 11.4) ---
    pore_base_kpa: float = 6.0
    pore_gain_kpa_per_vwc: float = 190.0     # rises with shallow saturation
    tilt_base_mg: float = (0.0, 0.0, 1000.0) # x, y, z (z = gravity, sensor up)
    tilt_drift_mg_full: float = 34.0         # x/y drift once the slope is fully loaded

    # --- atmosphere at U (understory ~2 m) ---
    air_temp_dry_c: float = 33.0             # hot pre-monsoon afternoons
    air_temp_diurnal_c: float = 6.5
    air_temp_rain_drop_c: float = 7.0        # cooling under the burst
    rh_dry_pct: float = 46.0
    rh_diurnal_pct: float = 18.0
    rh_rain_rise_pct: float = 44.0
    pressure_base_hpa: float = 1004.0
    pressure_predrop_hpa: float = 4.5        # synoptic fall ahead of the burst
    wind_base_ms: float = 2.6
    wind_gust_ms: float = 6.5                # frontal gusts during the burst
    solar_noon_wm2: float = 950.0
    solar_cloud_factor: float = 0.25        # fraction of clear-sky under heavy cloud
    pm25_base_ugm3: float = 42.0
    pm25_washout_ugm3: float = 12.0         # rain scavenges particulate
    mox_tvoc_base_index: float = 60.0

    def all_bursts(self) -> tuple[RainBurst, ...]:
        return (*self.showers, self.main_burst)


@dataclass
class PhysicalState:
    t_h: float
    rain_rate_mm_h: float
    rain_accum_mm: float
    vwc_s1: float
    vwc_surface: float
    stage_m: float
    pore_kpa: float
    tilt_mg: tuple[float, float, float]
    air_temp_c: float
    rh_pct: float
    pressure_hpa: float
    wind_ms: float
    wind_dir_deg: float
    solar_wm2: float
    pm25_ugm3: float
    pm10_ugm3: float
    pm1_ugm3: float
    tvoc_index: float


def _burst_rate_mm_h(b: RainBurst, t_h: float) -> float:
    """A smooth raised-cosine intensity profile over the burst window whose
    integral over [start, end] is exactly b.total_mm. Rate is >= 0 everywhere,
    so the cumulative depth below is monotone by construction."""
    if not (b.start_h <= t_h < b.end_h):
        return 0.0
    span = b.end_h - b.start_h
    x = (t_h - b.start_h) / span
    mean_rate = b.total_mm / span
    return mean_rate * (1.0 - math.cos(2.0 * math.pi * x))


def _burst_depth_mm(b: RainBurst, t_h: float) -> float:
    """Closed-form integral of _burst_rate_mm_h from b.start_h to t_h (mm).
    Monotone non-decreasing in t_h."""
    if t_h <= b.start_h:
        return 0.0
    span = b.end_h - b.start_h
    te = min(t_h, b.end_h)
    x = te - b.start_h
    mean_rate = b.total_mm / span
    return mean_rate * (x - (span / (2.0 * math.pi)) * math.sin(2.0 * math.pi * x / span))


def _exp_decay_accum(t_h: float, bursts, gain: float, tau_h: float) -> float:
    """Sum over bursts of gain * (rain so far in this burst) decaying toward 0
    with time constant tau after the burst ends -- a leaky bucket."""
    total = 0.0
    for b in bursts:
        if t_h <= b.start_h:
            continue
        contrib = gain * _burst_depth_mm(b, t_h)
        if t_h > b.end_h:
            contrib *= math.exp(-(t_h - b.end_h) / tau_h)
        total += contrib
    return total


def physical_state(sc: VillageScenario, t_h: float, rng: np.random.Generator | None = None) -> PhysicalState:
    rng = rng or np.random.default_rng(sc.seed)
    bursts = sc.all_bursts()

    rate = sum(_burst_rate_mm_h(b, t_h) for b in bursts)
    accum = sum(_burst_depth_mm(b, t_h) for b in bursts)   # monotone tipping-bucket total

    vwc_s1 = min(sc.vwc_saturation,
                 sc.vwc_dry_s1 + _exp_decay_accum(t_h, bursts, sc.s1_infiltration, sc.s1_drain_tau_h))
    vwc_surface = min(sc.vwc_saturation,
                      sc.vwc_dry_surface + _exp_decay_accum(t_h, bursts, sc.surface_infiltration, sc.surface_drain_tau_h))

    # river stage: routed + lagged rain, then exponential recession
    stage = sc.stage_base_m + _exp_decay_accum(max(0.0, t_h - sc.stage_lag_h), bursts,
                                               sc.stage_gain_m_per_mm, sc.stage_recess_tau_h)

    sat_frac = (vwc_s1 - sc.vwc_dry_s1) / (sc.vwc_saturation - sc.vwc_dry_s1)
    pore = sc.pore_base_kpa + sc.pore_gain_kpa_per_vwc * max(0.0, vwc_s1 - sc.vwc_dry_s1)
    drift = sc.tilt_drift_mg_full * max(0.0, sat_frac) ** 1.5
    bx, by, bz = sc.tilt_base_mg
    tilt = (bx + 0.7 * drift + float(rng.normal(0, 0.4)),
            by + 1.0 * drift + float(rng.normal(0, 0.4)),
            bz - 0.05 * drift + float(rng.normal(0, 0.4)))

    diur = -math.cos(2 * math.pi * (t_h % 24 - 3) / 24)     # min ~03:00, max ~15:00
    rain_env = min(1.0, rate / 3.0)
    air_t = sc.air_temp_dry_c + 0.5 * sc.air_temp_diurnal_c * diur - sc.air_temp_rain_drop_c * rain_env
    rh = min(99.0, sc.rh_dry_pct - 0.5 * sc.rh_diurnal_pct * diur + sc.rh_rain_rise_pct * rain_env)
    # synoptic pressure fall in the ~24 h before the main burst, partial recovery after
    mb = sc.main_burst
    if t_h < mb.start_h:
        pre = max(0.0, 1.0 - (mb.start_h - t_h) / 24.0)
        press = sc.pressure_base_hpa - sc.pressure_predrop_hpa * pre
    else:
        press = sc.pressure_base_hpa - sc.pressure_predrop_hpa * math.exp(-(t_h - mb.start_h) / 40.0)
    press += 1.1 * math.sin(2 * math.pi * (t_h % 24) / 12)   # semidiurnal tide

    wind = sc.wind_base_ms + (sc.wind_gust_ms - sc.wind_base_ms) * rain_env + abs(float(rng.normal(0, 0.5)))
    wind_dir = (215.0 + 25.0 * rain_env + float(rng.normal(0, 6))) % 360.0
    hod = t_h % 24
    clear_sky = max(0.0, sc.solar_noon_wm2 * math.sin(math.pi * (hod - 6) / 12)) * (6 <= hod <= 18)
    solar = clear_sky * (1.0 - (1.0 - sc.solar_cloud_factor) * rain_env)
    pm25 = max(3.0, sc.pm25_base_ugm3 - (sc.pm25_base_ugm3 - sc.pm25_washout_ugm3) * rain_env + float(rng.normal(0, 1.5)))
    tvoc = max(0.0, sc.mox_tvoc_base_index + float(rng.normal(0, 3)))

    return PhysicalState(
        t_h=t_h, rain_rate_mm_h=rate, rain_accum_mm=accum,
        vwc_s1=vwc_s1, vwc_surface=vwc_surface, stage_m=stage, pore_kpa=pore, tilt_mg=tilt,
        air_temp_c=air_t, rh_pct=rh, pressure_hpa=press, wind_ms=wind, wind_dir_deg=wind_dir,
        solar_wm2=solar, pm25_ugm3=pm25, pm10_ugm3=pm25 * 1.8, pm1_ugm3=pm25 * 0.7, tvoc_index=tvoc,
    )


# numpy 2.x dropped np.trapz alias for some builds; keep working either way
if not hasattr(np, "trapz"):
    np.trapz = np.trapezoid


def physical_series(sc: VillageScenario, dt_s: float) -> list[PhysicalState]:
    rng = np.random.default_rng(sc.seed)
    n = int(round(sc.duration_h * HOUR / dt_s))
    return [physical_state(sc, k * dt_s / HOUR, rng) for k in range(n + 1)]
