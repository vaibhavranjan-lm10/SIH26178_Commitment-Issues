# PRAHARI — System Blueprint

**Environmental Intelligence Network**
Distributed multi-hazard forecasting for India · SIH26178 · Version 2.0

---

## Contents

1. Design principle
2. Layer map
3. Transducers
4. Pods
5. Columns
6. Head node
7. Mesh
8. Cloud
9. Hazard heads
10. Physical build
11. Worked example — village deployment
12. Open items

---

## 1. Design principle

**Every node forecasts and detects on its own. The network improves it.
The cloud never decides.**

Three consequences follow, and everything in this document is downstream
of them:

- A node cut off from the mesh still produces every hazard output.
- A node cut off from the internet still produces every hazard output.
- Only the *rate at which the system learns* depends on connectivity.

The system is a **forecasting** system, not a threshold alarm. It emits a
probability with a calibrated confidence at a stated horizon, per hazard,
per site.

---

## 2. Layer map

```
L0  Transducers     raw physical measurement
L1  Pod             local cluster, features, instantaneous derivations
L2  Column          one measurement site — the unit of forecasting
L3  Head node       encoder + 9 hazard heads + uplink   ← ALL AUTHORITY
L4  Local mesh      optional peer refinement, headless
L5  Cloud           supervision, trends, retraining     ← NO INFERENCE
```

Failure is monotonic. No layer's loss disables the layer beneath it.

| Lost | Consequence |
|---|---|
| A transducer | One channel masked, model continues |
| A pod | Those channels masked, column continues |
| The mesh | Three heads lose refinement, all nine still run |
| The uplink | Alerts queue locally, forecasting continues |
| The cloud | Nothing real-time is lost |

---

## 3. Transducers (L0)

### 3.1 Interface classes

Four classes. Every sensor is one of these.

| Class | Interface | Sensors |
|---|---|---|
| A | I²C, 3.3 V, under 30 cm | Air T/RH, pressure, gas MOX, IR thermopile, IMU |
| B | UART | Particulate sensor, GNSS, digital anemometer |
| C | Analogue into ADC | Capacitive soil probes, MOX gas, water quality probes |
| D | Pulse / interrupt | Tipping rain gauge, cup anemometer, wind vane |

**I²C never leaves its own pod.** The bus is specified for on-board runs
of tens of centimetres. Any attempt to run one I²C bus up a mast will fail
intermittently in humidity and be extremely difficult to diagnose in the
field. Each pod terminates its own bus.

### 3.2 Enclosure of transducers

Transducers are **not** boxed with the pod electronics. They are the only
part of the system that must touch the environment, and sealing them
defeats them.

| Transducer | Housing | Why |
|---|---|---|
| Soil probes | Bare, potted at cable entry only | Must contact soil directly |
| Air T/RH | Inside a vented radiation shield, outside the pod box | Must see moving air, must not see sun |
| Particulate | Inside the pod box with a filtered inlet/outlet path | Needs airflow, must not flood |
| Gas MOX | Vented membrane port on the pod box | Needs diffusion, must not wet |
| Rain gauge | Own funnel body, external | Needs clear sky |
| Anemometer | External at canopy | Needs clean airflow |
| Ultrasonic level | External, downward, under a drip shield | Needs line of sight to water |
| IMU / tilt | **Inside** the pod box, rigidly bonded to the mast | Measures the mast, not the air |
| Water quality probes | Submerged, on a tether | Must be in the water |

**Rule:** the pod box holds electronics. Transducers mount on or through
the box wall with sealed glands. A transducer sealed inside a waterproof
box measures the inside of the box.

### 3.3 Mandatory mechanical requirements

- **Radiation shield** over every air temperature and humidity sensor.
  Without it the sensor reads solar heating of its own housing. Multi-plate,
  white, vented, and 3D-printable. Not optional.
- **Soil probes horizontal into an undisturbed pit face.** A vertical
  insertion down a bored hole creates a preferential flow path, and the
  probe then measures the hole rather than the soil.
- **Rain gauge level, clear sky, away from the mast wake.**
- **Anemometer above canopy or on a boom clear of mast turbulence.**

### 3.4 Power to transducers

Every transducer is fed from its pod's switched 3.3 V rail through a load
switch under MCU control. Nothing stays energised except the passive
pulse-counting rain gauge.

| Transducer | Draw when active | Duty |
|---|---|---|
| Particulate (fan) | ~80 mA | 30 s every 15 min |
| Gas MOX (heater) | ~40 mA | on only when armed |
| Soil probe excitation | ~10 mA | 200 ms per read |
| Ultrasonic level | ~50 mA | 100 ms per read |
| Air T/RH | <1 mA | continuous is acceptable |

---

## 4. Pods (L1)

### 4.1 Definition

One board, one MCU, one local transducer cluster, one position. Six pod
*positions*, one pod *design*, populated differently.

| Pod | Position | Ingress |
|---|---|---|
| S1 | −10 cm | IP68, potted |
| S2 | −40 cm | IP68, potted |
| S3 | −100 cm | IP68, potted |
| G | ground, 0 m | IP67 |
| U | understory, ~2 m | IP65 + shield |
| C | canopy/mast, ~10 m | IP65 + shield |

S1–S3 are normally one board with three probe leads, not three boards.
Three logical pods, one physical unit.

### 4.2 MCU

**STM32L031 / L051.** 32-bit, low stop-mode current, same toolchain as the
head node's MCU side. The compute load is trivial; **sleep current is the
governing constraint**, not clock speed.

Pods perform only instantaneous derivations, so no history buffer is
needed at this tier and 8 KB RAM suffices.

### 4.3 What a pod computes

| Function | Detail |
|---|---|
| Acquisition | Sample at configured cadence |
| Calibration | Two-point per channel, coefficients in EEPROM |
| Spike rejection | Median-of-5 |
| Range gating | Flag out-of-spec rather than discard |
| Derivations | Vapour pressure deficit, dew point, wind vector u/v, heat index |
| Anomaly gate | Z-score against rolling mean; raises a wake flag |
| Self-report | Own supply rail, transducer fault flags |

**Nothing history-dependent runs here.** Antecedent precipitation indices,
infiltration-front tracking and the fire-weather cascade all require state
across days and belong at L3.

### 4.4 Two connection modes

The distinction is not a preference. It is set by whether a pod can power
itself.

**Mode W — wired.** Co-located pods on the mast and in the ground beneath
it. A subsurface pod cannot carry a solar panel, so it is wired regardless.

- One 4-conductor cable in conduit up and down the mast: 12 V, GND,
  RS-485 A, RS-485 B
- Half-duplex multi-drop RS-485, head node is bus master, pods polled by
  address
- Differential signalling, immune to mast electrical noise, specified for
  hundreds of metres
- Local 12 V → 3.3 V buck on each pod
- 120 Ω termination at both bus ends
- Galvanically isolated transceivers so a strike on the mast cannot
  propagate into the head node

The cable is inside the sealed system, on your own mast, in conduit. It is
not a maintenance liability.

**Mode R — radio.** Pods that are genuinely somewhere else.

- SX1262, own small solar panel, own battery, own IP67 enclosure
- Reports to the head node on a scheduled slot
- Typical: water level at a riverbank, rain gauge in a clearing, a probe
  on the far side of a slope

Cabling to a remote location is the actual maintenance problem — rodents,
theft, gland failure at every crossing, and a fault that takes a site
visit to find. Radio for offset pods is the correct answer.

**Consequence to state:** an offset pod measures a *different place*. It
feeds the column vector, but the forecast is for the mast. Where the
offset exceeds roughly 100 m, treat it as its own column.

---

## 5. Columns (L2)

### 5.1 What a column is

**One column = one measurement site = one vertex in the graph.**

The column is what makes forecasting physically possible. A single point
cannot forecast. A vertical profile can:

- Soil moisture at three depths gives the wetting front and the saturation
  deficit, which determine whether rain infiltrates or runs off.
- Canopy-minus-understory temperature is atmospheric stability, which
  governs whether a smoke plume lofts or hugs the ground.
- The vertical humidity gradient is the fuel drying profile.

### 5.2 Physical arrangement

```
        [ C ]  canopy pod          ~10 m
          │
        [ U ]  understory pod      ~2 m
          │                        4-core cable in conduit
     [ HEAD NODE ]                 ~1.5 m   ← bus master
          │
        [ G ]  ground pod          0 m
          │
     ┌────┴────┬─────────┐
   [ S1 ]   [ S2 ]    [ S3 ]       −10 / −40 / −100 cm

              (( R ))              offset pod, radio, own solar
```

The head node polls each pod address in sequence, one full sweep per
cadence tick. A silent pod is marked missing and its channels are masked
at the model input. It does not stall the sweep.

### 5.3 Column profiles

Same mast, same cable, same head node — different pod population.

| Profile | Wired pods | Typical offset pod | Heads active |
|---|---|---|---|
| Riverine | S1–S3, G, U | Water level at bank | flood, landslide, heat |
| Forest | S1–S3, G, U, C | Rain gauge in clearing | fire, landslide, flood, heat |
| Urban | G, U, C | Drain level sensor | urban flood, pollution, heat |
| Industrial | G, U | Perimeter gas pod | gas leak, pollution, water quality |
| Village | S1, G, U | Water level or rain gauge | all, reduced fidelity |

---

## 6. Head node (L3)

### 6.1 Hardware

Two processors, one board.

| Side | Part | Role |
|---|---|---|
| MCU | STM32U585 | RS-485 bus master, LoRa timing, power sequencing, watchdog, GNSS pulse capture |
| Linux | Quad Cortex-A53 @ 2.0 GHz, 2 GB LPDDR4 | Model inference, uplink, local store |

The MCU side stays awake at low power. The Linux side is duty-cycled —
woken for an inference cycle, then suspended.

### 6.2 Radios

| Radio | Purpose |
|---|---|
| SX1262 LoRa | Peer mesh, 865–867 MHz ISM, licence-free in India |
| GSM / NB-IoT | Uplink to cloud, LTE-M with 2G fallback |
| GNSS | Time and position |
| Wi-Fi | Field servicing only, not operational |

### 6.3 Satellite data

**The node has no satellite receiver and no dish.**

Reanalysis and Earth-observation products are distributed from ground data
centres over the internet. They reach the head node **through the
GSM/NB-IoT uplink**, pushed from the cloud as a compact context vector of
a few dozen bytes, updated hourly to daily. It is not a live downlink.

**GNSS is the one genuine satellite reception**, and it does two jobs:

1. **Position** at install, populating the static site parameters.
2. **Time.** The pulse-per-second output disciplines the mesh TDMA
   schedule. Headless slotted access has no common clock without it. This
   is the more important of the two.

For sites with no cellular coverage at all, a satellite IoT modem may be
fitted as an *uplink alternative*. That is backhaul, not Earth-observation
reception.

### 6.4 Power

```
Solar 30 W → MPPT controller → LiFePO4 12.8 V 20 Ah
                                    │
              ┌─────────────────────┼──────────────────┐
         12 V mast bus          5 V buck           3.3 V buck
         (pods, RS-485)         (compute)          (radios)
```

**LiFePO4, not lithium-ion** — wider temperature tolerance, safer in a
sealed box in Indian summer, 2000+ cycles.

**Risk-adaptive duty cycling.** Inference cadence scales with assessed
risk: hourly at baseline, every 5 minutes once any head crosses a warning
threshold. Power follows risk rather than a fixed clock.

**Autonomy target: 7 days without sun.** This covers a monsoon overcast
period — precisely when flood risk peaks and the node must not die.

### 6.5 Thermal

Sustained inference inside a sealed enclosure at 45 °C ambient will
throttle the processor if heat has nowhere to go.

**Solution:** heatsink on the compute module, thermal pad bonded to an
**aluminium enclosure face**. The enclosure becomes the radiator. A
heatsink inside a sealed plastic box only moves heat into trapped air.

### 6.6 Surge and lightning

A 10 m mast in monsoon country is an antenna whether intended or not.
This is a hard requirement, not an enhancement.

| Point | Protection |
|---|---|
| RS-485 pair | Gas discharge tubes + TVS, galvanically isolated transceivers |
| Antenna feeds | Coaxial arrestors, both LoRa and cellular |
| Power input | TVS on the 12 V rail |
| Mast | Air terminal above all antennas |
| Ground | Earth rod at base, bonded, target under 10 Ω |

Design to IEC 61643. Every external connection is protected.

### 6.7 Software stack

**Base:** Debian on the Linux side.
**Runtime:** ONNX Runtime aarch64, CPU execution provider, 4 threads,
INT8 quantised.

Execution order per inference cycle:

| Stage | Operation | Params |
|---|---|---|
| 1 | Normalise, mask missing channels | — |
| 2 | Pretrained temporal encoder → 16-dim embedding | ~1 M |
| 3 | *Optional:* graph aggregation, K=2, over LoRa | ~30 k |
| 4 | Nine hazard heads — t+0 detection, t+Δ forecast | ~10 k each |
| 5 | Temperature scaling → calibrated confidence | 9 scalars |

**Stage 3 is skippable.** Stages 1, 2, 4 and 5 always run. That code path
*is* the standalone-operation claim.

**Local persistence:** SQLite ring buffer holding the rolling context
window plus ~30 days of history. A reboot mid-monsoon must not lose
antecedent precipitation state.

**Model versioning:** each node reports the weight hash it is running.
Post-event review must be able to establish which model produced which
forecast.

### 6.8 Alert generation

The head node — not the cloud — decides and emits.

- CAP-formatted alert, severity graded, confidence attached
- Queued locally if the uplink is down, transmitted on restoration
- Local siren or GPIO relay output for immediate community warning
- Two-tier warning structure: an advisory level when a hazard probability
  first crosses its lower threshold, and a warning level at the upper
  threshold with a shorter horizon

Thresholds are **not preset**. They are set per hazard from the
precision-recall curve on validation data, because the cost of a missed
event and the cost of a false alarm are not symmetric.

---

## 7. Mesh (L4)

### 7.1 Topology

**Headless.** No coordinator, no root. Every head node is a peer and each
reaches the cloud independently through its own uplink. Losing any node
removes one vertex — it does not partition the network or orphan anyone.

### 7.2 Scope

The mesh is **local, not national**. Message passing only carries meaning
within one coherent physical domain: a catchment, a forest block, an
airshed. Two nodes a thousand kilometres apart have no informative edge.

What spans the country is the deployment pattern and the shared model
weights — many independent local meshes, one globally trained model, one
cloud observing all of them.

Do not claim a single connected national graph.

### 7.3 PHY and MAC

| Setting | Value |
|---|---|
| Band | 865–867 MHz ISM, licence-free in India |
| PHY | LoRa, SF7–SF10 adaptive, 125 kHz |
| MAC | Custom TDMA, GNSS-disciplined slots |
| Payload | 16 B int8 embedding + 4 B header |
| Cadence | One round per inference cycle |
| Hops | K = 2 |

**On LoRaWAN.** LoRaWAN is star-topology and has no node-to-node path, so
it cannot carry peer refinement. PRAHARI uses LoRa PHY with a custom mesh
MAC for the peer layer, and NB-IoT or 5G for uplink — two of the four
protocol families named in the requirement, whose wording is "protocols
such as", a list of examples rather than a mandate.

### 7.4 Protocol behaviour

1. **Neighbour discovery** — periodic beacon, neighbours ranked by RSSI
   and distance.
2. **Adjacency** — a distance and terrain prior in forward and reverse
   directions, combined with a learned adaptive matrix. Not a hand-coded
   directed graph: water routes downslope while fire spreads upslope, so
   no single static direction can serve both heads.
3. **Slot assignment** — from node ID hash and GNSS time.
4. **Exchange** — broadcast own embedding, listen for neighbours.
5. **Aggregate** — neighbourhood aggregation over whatever arrived.
6. **Timeout** — a late or missing neighbour is dropped for that round.
   The node falls back to its own embedding.

### 7.5 What crosses the mesh

Only 16-byte embeddings. Never raw telemetry, never images. A column may
sense 80 channels; its neighbours receive 16 bytes. This is how the
bandwidth requirement is met structurally rather than by compression.

---

## 8. Cloud (L5)

### 8.1 Boundary

> **The cloud learns. The nodes decide.**
> Nothing in the decision path crosses the uplink.

If the cloud is unreachable, nodes keep forecasting on their last-pushed
weights indefinitely. Only the rate of improvement degrades.

### 8.2 Hosting

**Demonstration:** AWS Mumbai (ap-south-1) — in-country region, free tier
sufficient, hours to stand up.

**Stated production path:** MeghRaj / NIC National Cloud. For a system
serving national and state disaster authorities, data residency on
government infrastructure is the correct answer.

Say both: *demonstrated on AWS Mumbai, architected for MeghRaj.*

### 8.3 Stack

| Function | Service |
|---|---|
| Ingest | MQTT broker |
| Time series | TimescaleDB on managed Postgres |
| Objects | S3 — model artifacts, archives |
| Retraining | Scheduled compute job |
| Registry | MLflow |
| Geospatial | PostGIS + tile server |
| Visualisation | Grafana / dashboard on a shared API |

Each head node publishes independently. No coordinator, so no single
ingest point whose loss silences a region.

### 8.4 Three jobs, none real-time

1. **Long-term trend analysis** — seasonal aggregation, multi-year hazard
   frequency, site degradation, policy reporting.
2. **Retraining** — new labelled events append to the global set; updated
   weights ship to nodes as versioned ONNX. Per-site adaptation is a
   fine-tune of the **static embedding layer only**, not a full retrain.
3. **Fleet health** — the eight operational parameters: battery
   trajectory, link quality, calibration drift, node-silence detection.

### 8.5 Remote maintenance

This is what "low-maintenance" means in practice — **no truck rolls**.

| Capability | Mechanism |
|---|---|
| Remote diagnosis | Operational parameters streamed continuously |
| Predictive replacement | Battery and calibration drift trends flag a part before it fails |
| Remote recovery | Watchdog reset, staged firmware and model OTA with rollback |
| Graceful degradation | A dead pod masks its channels; the node keeps forecasting |
| Silence detection | Cloud raises a ticket when a node misses N reporting windows |

A site visit should be triggered by a *prediction* of failure, not by a
discovery of it.

---

## 9. Hazard heads (L3)

Nine heads on one shared encoder. All run at every node. Multi-task
learning, one encoder, nine outputs.

| Head | Horizon | Graph stage | Status |
|---|---|---|---|
| Riverine flood | t+0, t+24 h | yes | trained |
| Urban / flash flood | t+0, t+6 h | yes | trained |
| Forest fire | t+0, t+24 h | yes | trained |
| Air pollution | t+0, t+24 h | yes | trained |
| Landslide | t+0, t+24 h | no | trained |
| Extreme heat | t+72 h | no | trained |
| Cyclone proximity | t+48 h | no | trained |
| Industrial gas leak | t+0 | no | declared |
| Water quality degradation | t+0, t+24 h | no | declared |

Each head emits a probability and a calibrated confidence score.

Only the four spatially-propagating hazards use the graph stage. Point-source
hazards — gas leak, water quality, heat, landslide — gain nothing from
neighbour information and bypass it.

**Parameters:** 146 total — 82 primary, 39 secondary, 17 tertiary, 8
operational. Training is restricted to a ~41-channel subset, matching
model capacity to available data. The remainder are carried as declared
architecture channels.

---

## 10. Physical build

### 10.1 PCB designs required

Three boards.

| Board | Contents |
|---|---|
| **Pod board** | One design, populated per pod type. STM32L0, isolated RS-485 transceiver, 12 V→3.3 V buck, load switches, transducer headers, potting-compatible outline |
| **Head node carrier** | Hosts the compute module. RS-485 master, MPPT input, battery management, cellular socket, SX1262, GNSS, full surge protection |
| **Probe interface** | Excitation and analogue front end for three capacitive soil probes |

Mode R pods use the pod board plus a small solar/charge daughterboard.

### 10.2 Enclosures

| Part | Requirement |
|---|---|
| Subsurface pod | IP68, fully potted, single sealed gland |
| Ground pod | IP67, mud-tolerant, replaceable desiccant |
| Understory / canopy pod | IP65 inside a vented radiation shield |
| Head node | IP66, aluminium face for thermal path, hinged service door |
| Offset pod | IP67, integrated small solar panel |
| Radiation shield | Multi-plate, white, vented, printable |
| Solar mount | Adjustable tilt for latitude |

### 10.3 Deployment profiles by cost

Modularity means price tiers, not just pod counts.

| Tier | Cost | Instrumentation | Role |
|---|---|---|---|
| Village | ₹20–30 k | Commodity-grade | Dense coverage where nothing exists today |
| District | ₹60–90 k | Mixed, calibrated | Operational monitoring |
| Reference | ₹1.5–2 L | Research-grade, traceable | Anchors and cross-calibrates nearby cheap nodes |

The honest framing: a regulatory-grade monitoring station costs roughly a
crore. PRAHARI is not competing with those. It fills the gap between one
crore and *nothing at all*, which is what covers most of the country. Fifty
₹25 k nodes beat one crore-rupee station on spatial coverage, and the
reference tier keeps them honest.

**Stated limitation:** commodity gas sensors drift and cross-respond.
Cross-calibration against a reference node is scheduled, not assumed.

### 10.4 What the finished product looks like

A galvanised mast about 10 m tall. A solar panel on a tilt bracket near
the top. A grey enclosure the size of a small briefcase at chest height,
with a stubby whip antenna and a GNSS puck above it. Conduit running up
the mast to two small shielded units, and down into the ground to a buried
junction where three soil probes fan out horizontally into an undisturbed
pit face. Somewhere within a hundred metres, a small self-contained box on
a short post beside the water.

Nothing about it looks expensive. That is the point.

---

## 11. Worked example — village deployment

### 11.1 The site

A hill-foot village of roughly 1,200 people. A seasonal river runs along
the eastern edge. Terraced fields rise to the north. Reserved forest
covers the slope to the north-west. The road in follows the valley floor
from the south. A primary school and panchayat office sit at the centre.

The village is exposed to five of the nine hazards: **flash flooding** from
the river, **landslide** from the cut slope above the road, **forest fire**
on the north-west slope, **extreme heat** in the pre-monsoon months, and
**air pollution** during post-harvest burning.

One head node covers it. This is a **Village-profile column** at ₹20–30 k.

### 11.2 Site layout

```
              N
    ╔═══════════════════════╗
    ║   FOREST SLOPE        ║
    ║      ▲ fire risk      ║
    ║   ┌───────┐           ║
    ║   │  (R2) │ fire pod  ║      (R2)  offset — forest edge
    ║   └───────┘           ║
    ╟───────────────────────╢
    ║  CUT SLOPE  ▲ landslide║
    ║      (R3) tilt pod    ║      (R3)  offset — slope toe
    ╟───────────────────────╢    ┌──┐
    ║   TERRACED FIELDS     ║    │  │
    ║                       ║    │R │ ← seasonal river
    ║      ███ MAST ███     ║    │I │
    ║      ║ HEAD NODE ║    ║    │V │
    ║      ║ S1 G U    ║    ║    │E │
    ║   ▲ panchayat + school║    │R │
    ║                       ║    │  │
    ║              (R1) ────╫────┤  │   (R1)  offset — river stage
    ╚═══════════════════════╝    └──┘
```

### 11.3 Where the head node goes

**On the roof of the panchayat office**, or on a mast in its compound.

Four reasons, in order of weight:

1. **The requirement is to forecast to local authorities.** The panchayat
   *is* the local authority. Co-locating the node with the people who act
   on its output removes every intermediary. A siren on this mast is heard
   by the village directly.
2. **Central position.** Roughly equidistant from river, slope and forest,
   so radio reach to all three offset pods is comparable.
3. **It is the one place in the village with mains power, a locked
   compound, and someone responsible for it.** Mains is a *backup* charger
   only — the node runs on solar and must survive the grid being down,
   which during a flood it will be.
4. **Height and sky view** for solar, GNSS and cellular.

**Where it must not go:** on the riverbank, on the slope, or in the
forest. A node placed at the point of greatest hazard is a node destroyed
by the first event it detects. The node observes hazards; it does not
stand in them.

### 11.4 Pod placement, and which hazard each serves

**Wired — on and under the mast (Mode W)**

| Pod | Position | Transducers | Serves |
|---|---|---|---|
| S1 | −10 cm, in the compound | Soil moisture, soil temperature | Flood, landslide, fire |
| G | ground, mast base | Rain gauge, surface soil moisture, IMU | Flood, landslide, all |
| U | 2 m on mast | Air T/RH in radiation shield, pressure, PM2.5/PM10, MOX gas | Pollution, heat, fire |

Three pods, one cable, all within four metres of the head node.

**Radio — offset (Mode R)**

| Pod | Location | Transducers | Serves |
|---|---|---|---|
| R1 | River bank, ~300 m E | Ultrasonic water level, turbidity | Flash flood |
| R2 | Forest edge, ~600 m NW | Air T/RH, MOX gas, smoke, IR surface temp | Forest fire |
| R3 | Cut slope toe, ~250 m N | IMU/tilt, soil moisture, pore pressure | Landslide |

Each offset pod is a self-contained IP67 box with its own small solar
panel, battery and SX1262. None is cabled. This is deliberate: a cable to
the riverbank crosses a footpath, a field boundary and a drainage line,
and every crossing is a future fault requiring a site visit.

### 11.5 Hazard-to-location mapping

| Hazard | Sensed at | Forecast for | Horizon |
|---|---|---|---|
| Flash flood | R1 stage, G rainfall, S1 soil moisture | Village + downstream | t+6 h |
| Landslide | R3 tilt and pore pressure, S1, G rainfall | Cut slope + road | t+24 h |
| Forest fire | R2 gas, smoke, T/RH; U wind | NW forest block | t+24 h |
| Extreme heat | U air T/RH | Village | t+72 h |
| Air pollution | U particulate and gas | Village | t+24 h |

Note that **every hazard draws on more than one pod.** Flash flood needs
river stage *and* rainfall *and* soil moisture, because a full soil profile
turns moderate rain into runoff while a dry profile absorbs it. This is the
reason for the column: no single sensor forecasts anything.

### 11.6 Transducer housing at this site

| Transducer | Housing |
|---|---|
| Soil probes (S1) | Bare, horizontal into a pit face, cable potted at the gland |
| Rain gauge (G) | Own funnel body on a short post, clear of the mast |
| IMU (G) | Sealed inside the pod box, bonded to the mast foot |
| Air T/RH (U) | Vented multi-plate radiation shield, outside the pod box |
| Particulate (U) | Inside the pod box, filtered inlet and outlet through the wall |
| Gas MOX (U, R2) | Vented membrane port in the box wall |
| Water level (R1) | External, downward-facing under a drip shield |
| Turbidity (R1) | Submerged on a tether, retrievable for cleaning |
| Tilt (R3) | Sealed in the pod box, box rigidly staked into the slope |

### 11.7 What happens during an event

Pre-monsoon rain begins. The G pod's rain gauge counts. S1 shows soil
moisture climbing toward saturation. The head node's antecedent
precipitation index has been rising for four days.

The encoder runs. The flash flood head crosses its advisory threshold at
t+6 h with 0.71 confidence. The node raises its own duty cycle from hourly
to five-minutely. A CAP advisory is queued and sent over cellular; the
panchayat dashboard shows amber.

Two hours later, R1 reports river stage rising at 8 cm/hour. The landslide
head, reading R3's tilt drift alongside pore pressure, crosses its own
threshold. The flash flood head moves to warning. **The siren on the mast
sounds. The panchayat office is three metres away.**

Then the cellular tower goes down, as it does in floods.

**Nothing stops.** The node keeps sensing, keeps inferring, keeps sounding
its siren, and keeps queueing CAP messages for when the link returns. The
neighbouring node four kilometres upstream is still reachable over LoRa,
and its embedding — carrying upstream rainfall the village has not yet
seen — is still sharpening the forecast.

That is the entire design justification, in one paragraph.

---

## 12. Open items

1. Calibration interval per transducer class
2. Precise data source selection and ingestion cadence
3. Five data-pipeline decisions blocking training sample construction:
   index provenance, mixed-cadence resampling, missing-data policy,
   normalisation scope, soil depth count
4. Per-hazard thresholds — to be set from precision-recall curves on
   validation data, not preset
5. Mode R offset distance limit before a pod becomes its own column
