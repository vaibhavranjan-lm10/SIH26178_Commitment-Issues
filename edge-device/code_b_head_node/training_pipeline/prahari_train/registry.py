"""Parameter registry — the 146-parameter PRAHARI taxonomy.

Every entry here is a transcription of docs/reference/prahari_parameters.md
(v2.0).  Nothing is invented: if a parameter is not in that document it is
not in this table, and every channel used anywhere in the training pipeline
must resolve to an ID in this table.

Tier counts (spec): 82 primary / 39 secondary / 17 tertiary / 8 operational
= 146.  Trained (★) subset: 41 = 16 in-situ + 11 satellite + 12 secondary
+ 2 tertiary.  ``verify_counts()`` asserts exactly this and is run by the
test-suite and at import time of the training script.

OFFLINE ONLY — this module is part of the training/export pipeline and is
never deployed to the head node.  The channel ordering it defines *is*
baked into the exported ONNX artifact (see ``trained_channels()``), which
the on-device runtime must reproduce byte-for-byte.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Iterable, Literal

Tier = Literal["primary", "secondary", "tertiary", "operational"]
Group = Literal["in_situ", "satellite", "static", "secondary", "tertiary", "operational"]

HAZARDS: tuple[str, ...] = ("FL", "UF", "FI", "PO", "LS", "HW", "CY", "DR", "GL", "WQ")
HAZARD_NAMES: dict[str, str] = {
    "FL": "riverine flood",
    "UF": "urban / flash flood",
    "FI": "forest fire",
    "PO": "air pollution",
    "LS": "landslide",
    "HW": "extreme heat",
    "CY": "cyclone",
    "DR": "drought",
    "GL": "gas leak",
    "WQ": "water quality",
}


@dataclass(frozen=True)
class Parameter:
    id: str                       # P1..P82, S1..S39, T1..T17, O1..O8
    name: str
    tier: Tier
    group: Group
    hazards: tuple[str, ...]      # hazard codes; () for operational
    trained: bool = False         # ★ in the spec
    unit: str | None = None       # in-situ / operational: physical unit
    cadence: str | None = None    # satellite: delivery cadence
    inputs: tuple[str, ...] = ()  # secondary / tertiary: parameter IDs in the "From" column
    derivation: str | None = None # secondary / tertiary: literal "From" text

    def __post_init__(self) -> None:
        for h in self.hazards:
            if h not in HAZARDS:
                raise ValueError(f"{self.id}: unknown hazard code {h!r}")


def _p(id, name, unit, hazards, trained=False, cadence=None, group="in_situ"):
    return Parameter(id=id, name=name, tier="primary", group=group, hazards=tuple(hazards.split()),
                     trained=trained, unit=unit, cadence=cadence)


def _sat(id, name, cadence, hazards, trained=False):
    return _p(id, name, None, hazards, trained, cadence=cadence, group="satellite")


def _static(id, name, hazards, trained=False):
    return _p(id, name, None, hazards, trained, group="static")


def _s(id, name, frm, inputs, hazards, trained=False):
    return Parameter(id=id, name=name, tier="secondary", group="secondary",
                     hazards=tuple(hazards.split()), trained=trained,
                     inputs=tuple(inputs.split()), derivation=frm)


def _t(id, name, frm, inputs, hazards, trained=False):
    return Parameter(id=id, name=name, tier="tertiary", group="tertiary",
                     hazards=tuple(hazards.split()), trained=trained,
                     inputs=tuple(inputs.split()), derivation=frm)


def _o(id, name, unit):
    return Parameter(id=id, name=name, tier="operational", group="operational", hazards=(), unit=unit)


_ALL_HAZARDS = " ".join(HAZARDS)

PARAMETERS: tuple[Parameter, ...] = (
    # ---------------- PRIMARY — in situ (46) ----------------
    # Soil (10)
    _p("P1", "Soil volumetric water content @ 10 cm", "m³/m³", "FL UF LS FI", True),
    _p("P2", "Soil volumetric water content @ 40 cm", "m³/m³", "FL LS", True),
    _p("P3", "Soil volumetric water content @ 100 cm", "m³/m³", "FL LS", True),
    _p("P4", "Soil temperature @ 10 cm", "°C", "FL LS HW"),
    _p("P5", "Soil temperature @ 40 cm", "°C", "LS HW"),
    _p("P6", "Soil temperature @ 100 cm", "°C", "LS"),
    _p("P7", "Soil bulk electrical conductivity @ 10 cm", "dS/m", "FL WQ"),
    _p("P8", "Soil bulk electrical conductivity @ 40 cm", "dS/m", "FL"),
    _p("P9", "Soil bulk electrical conductivity @ 100 cm", "dS/m", "FL"),
    _p("P10", "Surface soil moisture, 0–5 cm", "m³/m³", "FL UF FI LS", True),
    # Surface hydrology (2)
    _p("P11", "Water level / stage", "m", "FL UF WQ", True),
    _p("P12", "Rainfall accumulation", "mm", "FL UF LS CY", True),
    # Atmosphere (8)
    _p("P13", "Air temperature — understory", "°C", "FI PO HW FL", True),
    _p("P14", "Air temperature — canopy", "°C", "FI HW CY"),
    _p("P15", "Relative humidity — understory", "%", "FI PO HW", True),
    _p("P16", "Relative humidity — canopy", "%", "FI HW"),
    _p("P17", "Barometric pressure", "hPa", "CY FL HW", True),
    _p("P18", "Wind speed", "m/s", "FI PO CY", True),
    _p("P19", "Wind direction", "deg", "FI PO CY", True),
    _p("P20", "Global solar irradiance", "W/m²", "FI HW PO", True),
    # Air quality — ambient (11)
    _p("P21", "PM1.0", "µg/m³", "PO"),
    _p("P22", "PM2.5", "µg/m³", "PO FI", True),
    _p("P23", "PM10", "µg/m³", "PO", True),
    _p("P24", "Carbon monoxide", "ppm", "PO FI GL"),
    _p("P25", "Nitrogen dioxide", "ppb", "PO"),
    _p("P26", "Ozone", "ppb", "PO HW"),
    _p("P27", "Sulphur dioxide", "ppb", "PO GL"),
    _p("P28", "Ammonia", "ppb", "PO GL"),
    _p("P29", "Benzene", "µg/m³", "PO GL"),
    _p("P30", "Carbon dioxide", "ppm", "PO FI"),
    _p("P31", "Total volatile organic compounds", "index", "PO FI GL"),
    # Fire (2)
    _p("P32", "Ground surface skin temperature", "°C", "FI HW", True),
    _p("P33", "Smoke obscuration", "index", "FI PO", True),
    # Industrial gas leak (3)
    _p("P34", "Methane", "ppm", "GL"),
    _p("P35", "Combustible gas / LPG", "% LEL", "GL"),
    _p("P36", "Hydrogen sulphide", "ppm", "GL WQ"),
    # Water quality (5)
    _p("P37", "pH", "—", "WQ"),
    _p("P38", "Turbidity", "NTU", "WQ FL"),
    _p("P39", "Dissolved oxygen", "mg/L", "WQ"),
    _p("P40", "Conductivity / TDS", "µS/cm", "WQ"),
    _p("P41", "Water temperature", "°C", "WQ"),
    # Geotechnical (5)
    _p("P42", "Ground vibration / tilt — X axis", "mg", "LS"),
    _p("P43", "Ground vibration / tilt — Y axis", "mg", "LS"),
    _p("P44", "Ground vibration / tilt — Z axis", "mg", "LS"),
    _p("P45", "Pore water pressure", "kPa", "LS FL"),
    _p("P46", "Soil heat flux", "W/m²", "HW LS"),

    # ---------------- PRIMARY — satellite and reanalysis (36) ----------------
    # Precipitation and hydrology (5)
    _sat("P47", "Precipitation rate", "30 min", "FL UF LS CY", True),
    _sat("P48", "Forecast precipitation", "1–6 h", "FL UF LS CY"),
    _sat("P49", "Surface soil moisture", "~3 d", "FL LS FI", True),
    _sat("P50", "Root zone soil moisture", "3 h", "FL LS"),
    _sat("P51", "River discharge forecast", "1 d", "FL", True),
    # Thermal and fire (4)
    _sat("P52", "Land surface temperature — day", "1 d", "FI HW", True),
    _sat("P53", "Land surface temperature — night", "1 d", "HW"),
    _sat("P54", "Fire radiative power", "~12 h", "FI", True),
    _sat("P55", "Burned area", "1 mo", "FI"),
    # Reflectance and backscatter (6)
    _sat("P56", "Reflectance — red", "5 d", "FI"),
    _sat("P57", "Reflectance — near infrared", "5 d", "FI FL", True),
    _sat("P58", "Reflectance — shortwave infrared", "5 d", "FI FL", True),
    _sat("P59", "Reflectance — green", "5 d", "FL WQ"),
    _sat("P60", "SAR backscatter VV", "6–12 d", "FL UF", True),
    _sat("P61", "SAR backscatter VH", "6–12 d", "FL UF"),
    # Atmosphere (8)
    _sat("P62", "Aerosol optical depth", "1 d", "PO FI"),
    _sat("P63", "2 m air temperature", "1 h", "HW FI"),
    _sat("P64", "2 m dewpoint temperature", "1 h", "HW FI"),
    _sat("P65", "10 m wind — u component", "1 h", "FI PO CY"),
    _sat("P66", "10 m wind — v component", "1 h", "FI PO CY"),
    _sat("P67", "Surface pressure", "1 h", "CY FL"),
    _sat("P68", "Planetary boundary layer height", "1 h", "PO"),
    _sat("P69", "Snow mass", "3 h", "LS FL"),
    # Static geophysical (13)
    _static("P70", "Elevation", "FL UF LS FI", True),
    _static("P71", "Soil sand fraction", "FL LS"),
    _static("P72", "Soil silt fraction", "FL LS"),
    _static("P73", "Soil clay fraction", "FL LS"),
    _static("P74", "Soil bulk density", "FL LS"),
    _static("P75", "Land cover class", "FI PO UF", True),
    _static("P76", "Canopy height", "FI"),
    _static("P77", "Latitude / longitude", _ALL_HAZARDS, True),   # spec: "all"
    _static("P78", "Distance to geologic faults", "LS"),
    _static("P79", "Lithology / bedrock class", "LS"),
    _static("P80", "Distance to road network", "LS FI"),
    _static("P81", "Forest loss", "LS FI"),
    _static("P82", "Impervious surface fraction", "UF"),

    # ---------------- SECONDARY (39) ----------------
    # Hydrological (12)
    _s("S1", "Rainfall intensity", "d/dt P12", "P12", "FL UF LS", True),
    _s("S2", "Antecedent precipitation index — 7 d", "P12 history", "P12", "FL LS", True),
    _s("S3", "Antecedent precipitation index — 30 d", "P12 history", "P12", "FL LS"),
    _s("S4", "Stage rate of rise", "d/dt P11", "P11", "FL UF", True),
    _s("S5", "Stage anomaly vs seasonal baseline", "P11 history", "P11", "FL"),
    _s("S6", "Soil moisture gradient — shallow", "P1 − P2", "P1 P2", "FL LS", True),
    _s("S7", "Soil moisture gradient — deep", "P2 − P3", "P2 P3", "FL"),
    _s("S8", "Profile-integrated soil water storage", "P1–P3", "P1 P2 P3", "FL LS"),
    _s("S9", "Saturation deficit", "P1–P3, P71–P74", "P1 P2 P3 P71 P72 P73 P74", "FL UF LS", True),
    _s("S10", "Infiltration front depth", "P1–P3 series", "P1 P2 P3", "FL LS"),
    _s("S11", "Saturated hydraulic conductivity", "P71–P74", "P71 P72 P73 P74", "FL UF LS"),
    _s("S12", "Soil temperature gradient", "P4 − P6", "P4 P6", "LS HW"),
    # Atmospheric (12)
    _s("S13", "Vapour pressure deficit", "P13, P15", "P13 P15", "FI HW", True),
    _s("S14", "Dew point", "P13, P15", "P13 P15", "HW FI"),
    _s("S15", "Wind vector u", "P18, P19", "P18 P19", "FI PO CY", True),
    _s("S16", "Wind vector v", "P18, P19", "P18 P19", "FI PO CY", True),
    _s("S17", "Wind gust factor", "P18 short-window", "P18", "FI CY"),
    _s("S18", "Vertical temperature lapse", "P14 − P13", "P14 P13", "FI PO", True),
    _s("S19", "Vertical humidity gradient", "P16 − P15", "P16 P15", "FI"),
    _s("S20", "Diurnal temperature range", "P13 over 24 h", "P13", "HW FI"),
    _s("S21", "Heat index", "P13, P15", "P13 P15", "HW"),
    _s("S22", "Ventilation coefficient", "P68 × P18", "P68 P18", "PO GL"),
    _s("S23", "Pressure tendency — 3 h / 24 h", "P17 history", "P17", "CY FL"),
    _s("S24", "Wind vector rotation rate", "S15, S16 history", "S15 S16", "CY"),
    # Remote sensing indices (5)
    _s("S25", "NDVI", "P56, P57", "P56 P57", "FI"),
    _s("S26", "NDMI", "P57, P58", "P57 P58", "FI"),
    _s("S27", "NDWI", "P59, P57", "P59 P57", "FL UF"),
    _s("S28", "Thermal inertia proxy", "P52 − P53", "P52 P53", "HW FI"),
    _s("S29", "SAR water mask", "P60, P61", "P60 P61", "FL UF"),
    # Terrain (6)
    _s("S30", "Slope", "P70", "P70", "FL UF LS FI", True),
    _s("S31", "Aspect — sin component", "P70", "P70", "FI LS"),
    _s("S32", "Aspect — cos component", "P70", "P70", "FI LS"),
    _s("S33", "Flow accumulation / upstream area", "P70", "P70", "FL UF", True),
    _s("S34", "Distance to nearest channel", "P70", "P70", "FL UF"),
    _s("S35", "Topographic wetness index", "S30, S33", "S30 S33", "FL LS", True),
    # Other (4)
    _s("S36", "PM2.5 / PM10 ratio", "P22, P23", "P22 P23", "PO FI"),
    _s("S37", "Tilt magnitude drift", "P42–P44 rolling", "P42 P43 P44", "LS"),
    _s("S38", "Fuel model class", "P75", "P75", "FI"),
    _s("S39", "Standardised precipitation index", "P12, P47 history", "P12 P47", "DR FL FI"),

    # ---------------- TERTIARY (17) ----------------
    # Fire weather cascade (7)
    _t("T1", "Fine fuel moisture code", "S13, S1, P13, P15, P18", "S13 S1 P13 P15 P18", "FI"),
    _t("T2", "Duff moisture code", "S2, P13, P15", "S2 P13 P15", "FI"),
    _t("T3", "Drought code", "S3, P13", "S3 P13", "FI DR"),
    _t("T4", "Initial spread index", "T1, S15, S16", "T1 S15 S16", "FI"),
    _t("T5", "Buildup index", "T2, T3", "T2 T3", "FI"),
    _t("T6", "Fire weather index", "T4, T5", "T4 T5", "FI", True),
    _t("T7", "Fire spread rate estimate", "T1, S30, S17, S38", "T1 S30 S17 S38", "FI"),
    # Flood and landslide (5)
    _t("T8", "Runoff generation potential", "S9, S1, S11", "S9 S1 S11", "FL UF"),
    _t("T9", "Catchment wetness state", "S8, S2, S35", "S8 S2 S35", "FL LS"),
    _t("T10", "Flash flood guidance ratio", "S1, T8", "S1 T8", "UF FL"),
    _t("T11", "Landslide susceptibility index", "S30, S9, S11, S37", "S30 S9 S11 S37", "LS", True),
    _t("T12", "Slope stability factor-of-safety proxy", "S30, P45, S9", "S30 P45 S9", "LS"),
    # Pollution and heat (3)
    _t("T13", "Atmospheric dispersion index", "S22, S18, S17", "S22 S18 S17", "PO GL"),
    _t("T14", "Accumulation potential", "T13 inverse, S36", "T13 S36", "PO"),
    _t("T15", "Heat stress accumulation", "S21, S20 multi-day", "S21 S20", "HW"),
    # Compound (2)
    _t("T16", "Cyclone proximity index", "S23, S24, P18, P67", "S23 S24 P18 P67", "CY"),
    _t("T17", "Drought severity index", "S39, T3, S8", "S39 T3 S8", "DR FI FL"),

    # ---------------- OPERATIONAL (8) ----------------
    _o("O1", "Battery voltage", "V"),
    _o("O2", "Battery state of charge", "%"),
    _o("O3", "Solar panel current", "mA"),
    _o("O4", "Node internal temperature", "°C"),
    _o("O5", "Link RSSI", "dBm"),
    _o("O6", "Link SNR", "dB"),
    _o("O7", "Packet delivery ratio", "%"),
    _o("O8", "Transducer calibration drift flag", "bitfield"),
)

BY_ID: dict[str, Parameter] = {p.id: p for p in PARAMETERS}
if len(BY_ID) != len(PARAMETERS):
    raise RuntimeError("duplicate parameter id in registry")

# Spec counts (prahari_parameters.md, "Summary" table).
SPEC_TOTAL = 146
SPEC_BY_TIER = {"primary": 82, "secondary": 39, "tertiary": 17, "operational": 8}
SPEC_TRAINED_TOTAL = 41
SPEC_TRAINED_SPLIT = {"in_situ": 16, "satellite": 11, "secondary": 12, "tertiary": 2}
# Spec "Parameters per hazard" table — overlapping counts.
SPEC_PER_HAZARD = {"FL": 62, "FI": 55, "LS": 47, "PO": 28, "UF": 24, "HW": 22,
                   "CY": 12, "GL": 10, "WQ": 9, "DR": 4}


# ---------------------------------------------------------------- lookups
def get(pid: str) -> Parameter:
    return BY_ID[pid]


def by_tier(tier: Tier) -> list[Parameter]:
    return [p for p in PARAMETERS if p.tier == tier]


def by_group(group: Group) -> list[Parameter]:
    return [p for p in PARAMETERS if p.group == group]


def by_hazard(hazard: str, trained_only: bool = False) -> list[Parameter]:
    if hazard not in HAZARDS:
        raise KeyError(hazard)
    return [p for p in PARAMETERS if hazard in p.hazards and (p.trained or not trained_only)]


def trained() -> list[Parameter]:
    """The 41-channel T1 subset, in registry (document) order."""
    return [p for p in PARAMETERS if p.trained]


def trained_channels() -> list[str]:
    """Channel order of the model input.  Frozen: the ONNX artifact depends on it.

    P77 (latitude / longitude) is a ★ static parameter that is a *pair* of
    numbers; the model input carries it as two channels P77_lat / P77_lon.
    Likewise P19 (wind direction) is ★ in the spec but the doc's own rule is
    that wind must enter the model as S15/S16, never as a raw bearing, so
    P19 is *not* a model input channel even though it is in the trained
    subset — it is trained *through* S15/S16.  Both facts are asserted in
    tests so the 41-count and the channel list stay reconciled explicitly.
    """
    chans: list[str] = []
    for p in trained():
        if p.id == "P19":
            continue  # enters as S15/S16 (spec, Atmospheric secondary note)
        if p.id == "P77":
            chans.extend(["P77_lat", "P77_lon"])
            continue
        chans.append(p.id)
    return chans


def dependencies(pid: str, transitive: bool = False) -> list[str]:
    p = BY_ID[pid]
    if not transitive:
        return list(p.inputs)
    seen: list[str] = []
    stack = list(p.inputs)
    while stack:
        q = stack.pop(0)
        if q in seen:
            continue
        seen.append(q)
        stack.extend(BY_ID[q].inputs)
    return seen


def topological_order(ids: Iterable[str] | None = None) -> list[str]:
    """Secondary/tertiary IDs ordered so every input precedes its consumer."""
    want = [i for i in (ids or [p.id for p in PARAMETERS if p.inputs])]
    order: list[str] = []
    done: set[str] = set()

    def visit(i: str) -> None:
        if i in done:
            return
        for d in BY_ID[i].inputs:
            if BY_ID[d].inputs:
                visit(d)
        done.add(i)
        order.append(i)

    for i in want:
        visit(i)
    return order


# ---------------------------------------------------------------- verification
def counts() -> dict:
    tier = {t: len(by_tier(t)) for t in SPEC_BY_TIER}
    tr = trained()
    # The spec's summary table folds the 13 static geophysical parameters into
    # "Primary — satellite / reanalysis" (5+4+6+8+13 = 36), so the trained
    # split counts group "static" under "satellite".
    split = {g: sum(1 for p in tr if p.group == g or (g == "satellite" and p.group == "static"))
             for g in SPEC_TRAINED_SPLIT}
    per_hazard = {h: len(by_hazard(h)) for h in HAZARDS}
    return {"total": len(PARAMETERS), "by_tier": tier, "trained_total": len(tr),
            "trained_split": split, "per_hazard": per_hazard}


def verify_counts(strict_per_hazard: bool = False) -> dict:
    """Assert the registry matches the spec.  Raises AssertionError otherwise.

    Per-hazard overlap counts are checked only when ``strict_per_hazard`` is
    set — see ``per_hazard_discrepancies()``; the document's own hazard table
    does not reconcile exactly with its per-row hazard tags and that is a
    source-document conflict to flag, not to silently resolve.
    """
    c = counts()
    assert c["total"] == SPEC_TOTAL, c
    assert c["by_tier"] == SPEC_BY_TIER, c
    assert c["trained_total"] == SPEC_TRAINED_TOTAL, c
    assert c["trained_split"] == SPEC_TRAINED_SPLIT, c
    assert len(trained_channels()) == SPEC_TRAINED_TOTAL, trained_channels()
    for p in PARAMETERS:
        for d in p.inputs:
            assert d in BY_ID, f"{p.id} depends on unknown {d}"
    if strict_per_hazard:
        assert c["per_hazard"] == SPEC_PER_HAZARD, c["per_hazard"]
    return c


def per_hazard_discrepancies() -> dict[str, tuple[int, int]]:
    """{hazard: (spec_count, registry_count)} for every hazard that differs."""
    c = counts()["per_hazard"]
    return {h: (SPEC_PER_HAZARD[h], c[h]) for h in HAZARDS if SPEC_PER_HAZARD[h] != c[h]}
