# PRAHARI — Parameter Reference

Environmental Intelligence Network · Version 2.0
Companion to the System Blueprint.

---

## Taxonomy

| Tier | Definition | Count |
|---|---|---|
| **Primary** | Ingested without computation — measured in situ, or downloaded as a satellite / reanalysis product | **82** |
| **Secondary** | Computed from one or more primaries | **39** |
| **Tertiary** | Computed from one or more secondaries | **17** |
| **Operational** | Node health telemetry; not a hazard input | **8** |
| | **Total** | **146** |

A satellite product downloaded as-is counts as primary even though it was
derived upstream. Where an index can be either downloaded or computed —
NDVI, for example — this list computes it, so the underlying bands are
primary and the index is secondary. Owning the computation means indices
can be recomputed when a band is cloud-masked.

**Training is restricted to the 41-parameter T1 subset**, marked ★. The
remainder are carried as declared architecture channels. This matches
model capacity to available hazard-positive samples.

## Hazard codes

`FL` riverine flood · `UF` urban / flash flood · `FI` forest fire ·
`PO` air pollution · `LS` landslide · `HW` extreme heat ·
`CY` cyclone · `DR` drought · `GL` gas leak · `WQ` water quality

---

# PRIMARY — in situ (46)

Measured by transducers on the column.

## Soil (10)

| # | Parameter | Unit | Hazards |
|---|---|---|---|
| ★ P1 | Soil volumetric water content @ 10 cm | m³/m³ | FL UF LS FI |
| ★ P2 | Soil volumetric water content @ 40 cm | m³/m³ | FL LS |
| ★ P3 | Soil volumetric water content @ 100 cm | m³/m³ | FL LS |
| P4 | Soil temperature @ 10 cm | °C | FL LS HW |
| P5 | Soil temperature @ 40 cm | °C | LS HW |
| P6 | Soil temperature @ 100 cm | °C | LS |
| P7 | Soil bulk electrical conductivity @ 10 cm | dS/m | FL WQ |
| P8 | Soil bulk electrical conductivity @ 40 cm | dS/m | FL |
| P9 | Soil bulk electrical conductivity @ 100 cm | dS/m | FL |
| ★ P10 | Surface soil moisture, 0–5 cm | m³/m³ | FL UF FI LS |

## Surface hydrology (2)

| # | Parameter | Unit | Hazards |
|---|---|---|---|
| ★ P11 | Water level / stage | m | FL UF WQ |
| ★ P12 | Rainfall accumulation | mm | FL UF LS CY |

## Atmosphere (8)

Sampled at two heights — understory ~2 m, canopy ~10 m.

| # | Parameter | Unit | Hazards |
|---|---|---|---|
| ★ P13 | Air temperature — understory | °C | FI PO HW FL |
| P14 | Air temperature — canopy | °C | FI HW CY |
| ★ P15 | Relative humidity — understory | % | FI PO HW |
| P16 | Relative humidity — canopy | % | FI HW |
| ★ P17 | Barometric pressure | hPa | CY FL HW |
| ★ P18 | Wind speed | m/s | FI PO CY |
| ★ P19 | Wind direction | deg | FI PO CY |
| ★ P20 | Global solar irradiance | W/m² | FI HW PO |

## Air quality — ambient (11)

| # | Parameter | Unit | Hazards |
|---|---|---|---|
| P21 | PM1.0 | µg/m³ | PO |
| ★ P22 | PM2.5 | µg/m³ | PO FI |
| ★ P23 | PM10 | µg/m³ | PO |
| P24 | Carbon monoxide | ppm | PO FI GL |
| P25 | Nitrogen dioxide | ppb | PO |
| P26 | Ozone | ppb | PO HW |
| P27 | Sulphur dioxide | ppb | PO GL |
| P28 | Ammonia | ppb | PO GL |
| P29 | Benzene | µg/m³ | PO GL |
| P30 | Carbon dioxide | ppm | PO FI |
| P31 | Total volatile organic compounds | index | PO FI GL |

P27–P29 are present because national ambient standards regulate them.
Omitting them forfeits any claim of alignment with Indian monitoring norms.

## Fire (2)

| # | Parameter | Unit | Hazards |
|---|---|---|---|
| ★ P32 | Ground surface skin temperature | °C | FI HW |
| ★ P33 | Smoke obscuration | index | FI PO |

Smoke is a distinct channel from PM2.5. Particulate mass rises for dust
and traffic; the combustion signature is what separates them.

## Industrial gas leak (3)

| # | Parameter | Unit | Hazards |
|---|---|---|---|
| P34 | Methane | ppm | GL |
| P35 | Combustible gas / LPG | % LEL | GL |
| P36 | Hydrogen sulphide | ppm | GL WQ |

## Water quality (5)

| # | Parameter | Unit | Hazards |
|---|---|---|---|
| P37 | pH | — | WQ |
| P38 | Turbidity | NTU | WQ FL |
| P39 | Dissolved oxygen | mg/L | WQ |
| P40 | Conductivity / TDS | µS/cm | WQ |
| P41 | Water temperature | °C | WQ |

Dissolved oxygen is meaningless without water temperature — saturation is
temperature-dependent.

## Geotechnical (5)

| # | Parameter | Unit | Hazards |
|---|---|---|---|
| P42 | Ground vibration / tilt — X axis | mg | LS |
| P43 | Ground vibration / tilt — Y axis | mg | LS |
| P44 | Ground vibration / tilt — Z axis | mg | LS |
| P45 | Pore water pressure | kPa | LS FL |
| P46 | Soil heat flux | W/m² | HW LS |

---

# PRIMARY — satellite and reanalysis (36)

Delivered to the head node as a compact context vector over the cellular
uplink. Not received by radio at the node.

## Precipitation and hydrology (5)

| # | Parameter | Cadence | Hazards |
|---|---|---|---|
| ★ P47 | Precipitation rate | 30 min | FL UF LS CY |
| P48 | Forecast precipitation | 1–6 h | FL UF LS CY |
| ★ P49 | Surface soil moisture | ~3 d | FL LS FI |
| P50 | Root zone soil moisture | 3 h | FL LS |
| ★ P51 | River discharge forecast | 1 d | FL |

## Thermal and fire (4)

| # | Parameter | Cadence | Hazards |
|---|---|---|---|
| ★ P52 | Land surface temperature — day | 1 d | FI HW |
| P53 | Land surface temperature — night | 1 d | HW |
| ★ P54 | Fire radiative power | ~12 h | FI |
| P55 | Burned area | 1 mo | FI |

## Reflectance and backscatter (6)

| # | Parameter | Cadence | Hazards |
|---|---|---|---|
| P56 | Reflectance — red | 5 d | FI |
| ★ P57 | Reflectance — near infrared | 5 d | FI FL |
| ★ P58 | Reflectance — shortwave infrared | 5 d | FI FL |
| P59 | Reflectance — green | 5 d | FL WQ |
| ★ P60 | SAR backscatter VV | 6–12 d | FL UF |
| P61 | SAR backscatter VH | 6–12 d | FL UF |

SAR is the only cloud-independent water observation. During an active
monsoon every optical band is masked exactly when flood risk peaks.

## Atmosphere (8)

| # | Parameter | Cadence | Hazards |
|---|---|---|---|
| P62 | Aerosol optical depth | 1 d | PO FI |
| P63 | 2 m air temperature | 1 h | HW FI |
| P64 | 2 m dewpoint temperature | 1 h | HW FI |
| P65 | 10 m wind — u component | 1 h | FI PO CY |
| P66 | 10 m wind — v component | 1 h | FI PO CY |
| P67 | Surface pressure | 1 h | CY FL |
| P68 | Planetary boundary layer height | 1 h | PO |
| P69 | Snow mass | 3 h | LS FL |

Snow mass matters in Himalayan deployment: falling rain melts snow, adding
water available to infiltrate and raise pore pressures.

## Static geophysical (13)

Observed once at install; constant thereafter. Held in node flash.

| # | Parameter | Hazards |
|---|---|---|
| ★ P70 | Elevation | FL UF LS FI |
| P71 | Soil sand fraction | FL LS |
| P72 | Soil silt fraction | FL LS |
| P73 | Soil clay fraction | FL LS |
| P74 | Soil bulk density | FL LS |
| ★ P75 | Land cover class | FI PO UF |
| P76 | Canopy height | FI |
| ★ P77 | Latitude / longitude | all |
| P78 | Distance to geologic faults | LS |
| P79 | Lithology / bedrock class | LS |
| P80 | Distance to road network | LS FI |
| P81 | Forest loss | LS FI |
| P82 | Impervious surface fraction | UF |

The static block is what lets globally trained weights localise. Per-site
adaptation is a fine-tune of the static embedding layer only, not a full
retrain.

**Primary total: 82** (46 in situ + 36 satellite)

---

# SECONDARY — computed from primaries (39)

## Hydrological (12)

| # | Parameter | From | Hazards |
|---|---|---|---|
| ★ S1 | Rainfall intensity | d/dt P12 | FL UF LS |
| ★ S2 | Antecedent precipitation index — 7 d | P12 history | FL LS |
| S3 | Antecedent precipitation index — 30 d | P12 history | FL LS |
| ★ S4 | Stage rate of rise | d/dt P11 | FL UF |
| S5 | Stage anomaly vs seasonal baseline | P11 history | FL |
| ★ S6 | Soil moisture gradient — shallow | P1 − P2 | FL LS |
| S7 | Soil moisture gradient — deep | P2 − P3 | FL |
| S8 | Profile-integrated soil water storage | P1–P3 | FL LS |
| ★ S9 | Saturation deficit | P1–P3, P71–P74 | FL UF LS |
| S10 | Infiltration front depth | P1–P3 series | FL LS |
| S11 | Saturated hydraulic conductivity | P71–P74 | FL UF LS |
| S12 | Soil temperature gradient | P4 − P6 | LS HW |

## Atmospheric (12)

| # | Parameter | From | Hazards |
|---|---|---|---|
| ★ S13 | Vapour pressure deficit | P13, P15 | FI HW |
| S14 | Dew point | P13, P15 | HW FI |
| ★ S15 | Wind vector u | P18, P19 | FI PO CY |
| ★ S16 | Wind vector v | P18, P19 | FI PO CY |
| S17 | Wind gust factor | P18 short-window | FI CY |
| ★ S18 | Vertical temperature lapse | P14 − P13 | FI PO |
| S19 | Vertical humidity gradient | P16 − P15 | FI |
| S20 | Diurnal temperature range | P13 over 24 h | HW FI |
| S21 | Heat index | P13, P15 | HW |
| S22 | Ventilation coefficient | P68 × P18 | PO GL |
| S23 | Pressure tendency — 3 h / 24 h | P17 history | CY FL |
| S24 | Wind vector rotation rate | S15, S16 history | CY |

Wind must enter the model as S15/S16, never as P19 directly. A raw bearing
wraps 359° → 0° and the model will learn a discontinuity that does not
exist in the world.

## Remote sensing indices (5)

| # | Parameter | From | Hazards |
|---|---|---|---|
| S25 | NDVI | P56, P57 | FI |
| S26 | NDMI | P57, P58 | FI |
| S27 | NDWI | P59, P57 | FL UF |
| S28 | Thermal inertia proxy | P52 − P53 | HW FI |
| S29 | SAR water mask | P60, P61 | FL UF |

## Terrain (6)

| # | Parameter | From | Hazards |
|---|---|---|---|
| ★ S30 | Slope | P70 | FL UF LS FI |
| S31 | Aspect — sin component | P70 | FI LS |
| S32 | Aspect — cos component | P70 | FI LS |
| ★ S33 | Flow accumulation / upstream area | P70 | FL UF |
| S34 | Distance to nearest channel | P70 | FL UF |
| ★ S35 | Topographic wetness index | S30, S33 | FL LS |

Aspect requires the same sin/cos treatment as wind direction, for the same
wrap-around reason.

## Other (4)

| # | Parameter | From | Hazards |
|---|---|---|---|
| S36 | PM2.5 / PM10 ratio | P22, P23 | PO FI |
| S37 | Tilt magnitude drift | P42–P44 rolling | LS |
| S38 | Fuel model class | P75 | FI |
| S39 | Standardised precipitation index | P12, P47 history | DR FL FI |

S36 separates combustion aerosol from wind-blown dust — cheap, and
genuinely discriminative between a fire signal and a dust storm.

**Secondary total: 39**

---

# TERTIARY — computed from secondaries (17)

Physics-derived composite indices. These add no information a network
could not learn from the primaries; they add **inductive bias**, which
substitutes for training data that does not exist and makes outputs
legible to a domain expert.

## Fire weather cascade (7)

Itself a cascade, so members sit at different depths. Grouped for
coherence.

| # | Parameter | From | Hazards |
|---|---|---|---|
| T1 | Fine fuel moisture code | S13, S1, P13, P15, P18 | FI |
| T2 | Duff moisture code | S2, P13, P15 | FI |
| T3 | Drought code | S3, P13 | FI DR |
| T4 | Initial spread index | T1, S15, S16 | FI |
| T5 | Buildup index | T2, T3 | FI |
| ★ T6 | Fire weather index | T4, T5 | FI |
| T7 | Fire spread rate estimate | T1, S30, S17, S38 | FI |

**The cascade requires an unbroken daily record.** A node that drops out
breaks T1–T7 outright, which makes the missing-data policy a hard
constraint on this tier rather than a modelling preference.

## Flood and landslide (5)

| # | Parameter | From | Hazards |
|---|---|---|---|
| T8 | Runoff generation potential | S9, S1, S11 | FL UF |
| T9 | Catchment wetness state | S8, S2, S35 | FL LS |
| T10 | Flash flood guidance ratio | S1, T8 | UF FL |
| ★ T11 | Landslide susceptibility index | S30, S9, S11, S37 | LS |
| T12 | Slope stability factor-of-safety proxy | S30, P45, S9 | LS |

## Pollution and heat (3)

| # | Parameter | From | Hazards |
|---|---|---|---|
| T13 | Atmospheric dispersion index | S22, S18, S17 | PO GL |
| T14 | Accumulation potential | T13 inverse, S36 | PO |
| T15 | Heat stress accumulation | S21, S20 multi-day | HW |

## Compound (2)

| # | Parameter | From | Hazards |
|---|---|---|---|
| T16 | Cyclone proximity index | S23, S24, P18, P67 | CY |
| T17 | Drought severity index | S39, T3, S8 | DR FI FL |

**Tertiary total: 17**

---

# OPERATIONAL — node health (8)

Not hazard inputs. These serve remote diagnosis, predictive maintenance,
risk-adaptive duty cycling, and the distinction between a degraded node
and a dead one.

| # | Parameter | Unit | Purpose |
|---|---|---|---|
| O1 | Battery voltage | V | Immediate health |
| O2 | Battery state of charge | % | Autonomy remaining |
| O3 | Solar panel current | mA | Harvest performance, panel soiling |
| O4 | Node internal temperature | °C | Thermal throttling detection |
| O5 | Link RSSI | dBm | Mesh and uplink quality |
| O6 | Link SNR | dB | Mesh and uplink quality |
| O7 | Packet delivery ratio | % | Degrading link detection |
| O8 | Transducer calibration drift flag | bitfield | Predictive replacement |

Without these there is no remote diagnosis, and "low-maintenance" means
nothing. A site visit should be triggered by a *prediction* of failure,
not a discovery of it.

**Operational total: 8**

---

# Summary

| Tier | Count | Trained (T1) |
|---|---|---|
| Primary — in situ | 46 | 16 |
| Primary — satellite / reanalysis | 36 | 11 |
| **Primary** | **82** | **27** |
| Secondary | 39 | 12 |
| Tertiary | 17 | 2 |
| Operational | 8 | 0 |
| **Total** | **146** | **41** |

## Parameters per hazard

One parameter frequently serves several hazards; these counts overlap.

| Hazard | Parameters |
|---|---|
| Riverine flood | 62 |
| Forest fire | 55 |
| Landslide | 47 |
| Air pollution | 28 |
| Urban / flash flood | 24 |
| Extreme heat | 22 |
| Cyclone | 12 |
| Gas leak | 10 |
| Water quality | 9 |
| Drought | 4 |

The heavy overlap between flood, fire and landslide — soil moisture,
rainfall and terrain feed all three — is the argument for one shared
encoder with multiple heads rather than three separate models. The
single-digit counts for gas leak, water quality and drought are why those
heads bypass the graph stage and why two of them remain declared but
untrained.

---

# Open decisions

These block construction of the first training sample.

1. **Index provenance** — download NDVI/NDMI as products, or compute from
   reflectance bands as listed here
2. **Resampling rule** across mixed cadences: soil at 15 min, wind at
   1 min, satellite at hours to days
3. **Missing-data policy** — mask or impute; binds the T1–T7 cascade
4. **Normalisation scope** — global or per-site; interacts with the
   per-site fine-tuning claim
5. **Soil depth count** — three or two; dropping to two removes 5
   primaries and 2 secondaries
