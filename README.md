# PRAHARI --- Environmental Intelligence Network

> **Edge-first, distributed multi-hazard environmental intelligence for
> rural and hill-foot communities.**

**Project:** PRAHARI --- Environmental Intelligence Network\
**SIH Problem Statement:** SIH26178\
**Version:** 2.0\
**Category:** Hardware\
**Status:** System blueprint / prototype architecture

------------------------------------------------------------------------

## 1. Overview

PRAHARI is a distributed, edge-first environmental monitoring and
forecasting network designed for communities that have limited or no
continuous hazard monitoring.

Instead of depending on one expensive monitoring station or a cloud
service for every decision, PRAHARI distributes low-cost sensor nodes
across an area. Each node collects environmental data, performs local
processing and forecasting, and communicates compact information with
nearby nodes using a LoRa-based mesh.

The system is designed so that **local forecasting and local alerts
continue even when cloud or cellular connectivity is unavailable**.

### Core principle

``` text
Sensors
   ↓
Local Edge Processing
   ↓
Hazard Forecast
   ↓
Local Alert
   ↕
LoRa Neighbour Nodes
   ↓
Cloud Learning / Monitoring
```

------------------------------------------------------------------------

## 2. Problem Statement

Many rural and hill-foot regions lack dense, real-time environmental
monitoring.

Conventional monitoring approaches can suffer from:

-   High infrastructure cost
-   Sparse monitoring locations
-   Dependence on cellular/cloud connectivity
-   Limited coverage of interacting hazards
-   Delayed communication with local communities
-   High maintenance requirements

PRAHARI addresses these limitations using a distributed, modular and
fault-tolerant architecture.

------------------------------------------------------------------------

## 3. Proposed Solution

PRAHARI uses three main layers:

### 3.1 Sensor Pods

Low-power sensor pods collect environmental measurements such as:

-   Air temperature and humidity
-   Atmospheric pressure
-   Particulate matter
-   Gas / MOX measurements
-   Soil moisture and temperature
-   Rainfall
-   Wind speed / direction
-   Water level
-   Water quality
-   IMU / tilt measurements

Pods can be connected to a head node using wired RS-485 or deployed
remotely using LoRa communication.

### 3.2 Head Node

The head node acts as the local intelligence and coordination point for
a deployment.

It provides:

-   Sensor data acquisition
-   Edge AI inference
-   GNSS positioning and time
-   LoRa communication
-   Cellular / NB-IoT communication
-   Local storage
-   RTC
-   Local alert control
-   Interfaces for expansion and debugging

### 3.3 Cloud Layer

The cloud is used for:

-   Data ingestion
-   Long-term storage
-   Model training and validation
-   Model versioning
-   Fleet monitoring
-   Dashboard visualisation
-   Threshold / model updates

The cloud is **not required for the local real-time warning path**.

------------------------------------------------------------------------

## 4. System Architecture

``` text
                    ┌──────────────────────────────┐
                    │          CLOUD LAYER         │
                    │                              │
                    │ MQTT → DB → MLflow →        │
                    │ PostGIS/Grafana → Model Ops │
                    └──────────────▲───────────────┘
                                   │
                              Cellular
                                   │
                                   │
┌───────────────┐            ┌─────┴────────────┐
│ Remote Pod 1  │            │                  │
│               │   LoRa     │    HEAD NODE     │
│ Soil / Rain   ├───────────►│                  │
│ Water / IMU   │            │ Edge AI          │
└───────────────┘            │ MCU + Compute     │
                             │ GNSS              │
┌───────────────┐            │ LoRa              │
│ Remote Pod 2  │   LoRa     │ Cellular          │
│               ├───────────►│ Local Storage     │
│ Air / Gas     │            │ Alert Controller  │
└───────────────┘            └─────────┬─────────┘
                                       │
                                  Local Alert
                                       │
                                       ▼
                                  ┌─────────┐
                                  │ Siren / │
                                  │ Relay   │
                                  └─────────┘

       Wired sensor pods
              │
              │ RS-485
              ▼
        ┌──────────────┐
        │  HEAD NODE   │
        └──────────────┘
```

------------------------------------------------------------------------

## 5. Multi-Hazard AI

PRAHARI follows an edge-first AI architecture.

Sensor observations are normalised and supplied to a shared temporal
encoder. The resulting representation can optionally be refined using
information from neighbouring nodes before being passed to
hazard-specific prediction heads.

### AI pipeline

``` text
Raw Sensor Data
      ↓
Normalisation
      ↓
Missing-Data Mask
      ↓
Shared Temporal Encoder
      ↓
Compact Node Embedding
      ↓
Optional Graph Aggregation
      ↓
9 Hazard-Specific Heads
      ↓
Probability + Confidence
      ↓
Local Alert / Cloud Reporting
```

The architecture is intended to support multiple environmental hazards
from a shared model while allowing hazard-specific outputs.

------------------------------------------------------------------------

## 6. Communication Architecture

### Local sensor communication

**RS-485**

Used for wired sensor pods where physical cabling is practical.

### Remote communication

**LoRa**

Used for low-power long-range communication with remote sensor pods and
neighbouring nodes.

### Node-to-node communication

PRAHARI uses a local LoRa mesh with a custom TDMA-style communication
approach.

Nodes exchange compact information rather than continuously sending
large raw datasets.

### Cloud communication

**GSM / NB-IoT**

Used to send telemetry, alerts and model-related information to the
cloud when connectivity is available.

------------------------------------------------------------------------

## 7. Hardware Architecture

### Sensor Node / Pod

The blueprint specifies low-power STM32L031 / STM32L051-class
controllers for sensor pods.

Typical sensor interfaces include:

-   I²C
-   UART
-   ADC
-   GPIO
-   Pulse inputs

### Head Node

The PRAHARI blueprint specifies a head-node architecture consisting of:

-   STM32U585-class MCU
-   Quad Cortex-A53 compute
-   2 GB LPDDR4-class memory
-   GNSS
-   SX1262 LoRa
-   GSM / NB-IoT
-   Wi-Fi service interface

> **Prototype note:** The current circuit illustration may use a
> different MCU representation (for example, STM32H743). The final
> implementation should keep the schematic, PCB, BOM and PPT consistent
> with the actual selected hardware.

------------------------------------------------------------------------

# 8. Power Supply

PRAHARI is designed primarily as a solar-powered system.

### Power architecture

``` text
             ┌──────────────┐
             │  Solar Panel │
             │    30 W      │
             └──────┬───────┘
                    │
                    ▼
             ┌──────────────┐
             │     MPPT     │
             │   Charger    │
             └──────┬───────┘
                    │
                    ▼
             ┌──────────────┐
             │  LiFePO₄     │
             │ 12.8 V 20 Ah │
             └──────┬───────┘
                    │
                    ▼
             ┌──────────────┐
             │ Protected    │
             │ 12 V Bus     │
             └──────┬───────┘
                    │
          ┌─────────┴─────────┐
          ▼                   ▼
   ┌────────────┐       ┌────────────┐
   │ 12 V → 5 V │       │12 V → 3.3 V│
   │   Buck     │       │    Buck    │
   └─────┬──────┘       └─────┬──────┘
         │                    │
         ▼                    ▼
   Compute / Radio       Sensors / MCU
```

### Power specifications

  Parameter           Target
  ------------------- -------------------------
  Solar panel         30 W, 18 V nominal
  Charge controller   MPPT
  Battery             LiFePO₄, 12.8 V, 20 Ah
  Distribution        Protected 12 V mast bus
  Logic rails         5 V and 3.3 V
  Target autonomy     7 days without sunlight

The design also calls for surge protection on the 12 V rail.

### Backup power

For village deployments, mains power can be used as a **backup charging
source** at the deployment site. The primary operating architecture
remains solar + battery.

------------------------------------------------------------------------

## 9. Power Protection

The power subsystem should include appropriate protection for the
deployment environment.

The conceptual power schematic includes:

-   Input fuse
-   Battery protection / fuse
-   12 V bus protection
-   Reverse-polarity protection
-   TVS surge protection
-   Bulk input/output capacitors
-   Separate regulated 5 V and 3.3 V rails

### Important

Component values shown in a conceptual schematic must be verified
against the selected regulator, battery, solar controller, maximum load
and manufacturer datasheets before PCB fabrication.

------------------------------------------------------------------------

## 10. Head Node Circuit Blocks

The complete head-node electronics are organised into the following
blocks:

``` text
┌─────────────────────────────────────────────┐
│                HEAD NODE PCB                │
├─────────────────────────────────────────────┤
│                                             │
│  POWER                                      │
│  Solar / Battery / 12 V / 5 V / 3.3 V      │
│                                             │
│  SENSOR INTERFACES                          │
│  I²C / UART / ADC / GPIO / Pulse            │
│                                             │
│  PROCESSING                                 │
│  MCU + Edge Compute                         │
│                                             │
│  COMMUNICATION                              │
│  LoRa / Cellular / RS-485 / GNSS            │
│                                             │
│  STORAGE                                    │
│  microSD / EEPROM / RTC                    │
│                                             │
│  OUTPUTS                                    │
│  Buzzer / Siren / Relay / LEDs              │
│                                             │
│  SERVICE                                    │
│  SWD / Reset / Boot / Expansion             │
└─────────────────────────────────────────────┘
```

------------------------------------------------------------------------

## 11. PCB Concept

The proposed PCB is divided into functional zones.

### Recommended zones

1.  **Power input and protection**
2.  **Battery / 12 V distribution**
3.  **5 V regulator**
4.  **3.3 V regulator**
5.  **MCU / compute**
6.  **Sensor connectors**
7.  **LoRa / cellular**
8.  **RS-485**
9.  **Storage / RTC**
10. **Debug and control**

### PCB layout principles

-   Keep high-current power traces short and wide.
-   Keep switching regulator sections away from sensitive analog inputs.
-   Provide adequate grounding.
-   Separate noisy digital/RF sections from sensitive analog sensor
    paths where practical.
-   Place surge protection close to external interfaces.
-   Provide suitable enclosure, ventilation and environmental
    protection.
-   Verify connector pinouts before fabrication.

------------------------------------------------------------------------

## 12. Fault Tolerance

PRAHARI is designed to degrade gracefully rather than stop completely.

### Sensor failure

``` text
Sensor Failure
     ↓
Channel Masking
     ↓
Remaining Sensors
     ↓
Forecast Continues
```

### Mesh failure

``` text
LoRa Mesh Failure
      ↓
No Neighbour Refinement
      ↓
Local Forecast Continues
```

### Cloud failure

``` text
Cloud Unavailable
      ↓
Local Model Still Runs
      ↓
Local Alert Still Works
```

### Cellular failure

``` text
Cellular Down
     ↓
Local Siren / Relay
     +
Queued Local Alert
```

This fault-tolerant behaviour is a core design principle of the system.

------------------------------------------------------------------------

## 13. Local Alerting

The system does not rely exclusively on remote dashboards.

The head node can control local outputs such as:

-   Active buzzer / siren
-   Optional relay
-   Status LEDs

A local alert can therefore be generated close to the affected
community.

The system can also queue alerts for later transmission when
connectivity becomes available.

------------------------------------------------------------------------

## 14. Deployment Model

PRAHARI is designed around modular deployment tiers.

### Village Tier

**Approx. ₹20--30k**

-   Dense sensor coverage
-   Local warning
-   Basic environmental monitoring

### District Tier

**Approx. ₹60--90k**

-   Operational deployment
-   More nodes
-   Better spatial coverage
-   Reference sensors

### Reference Tier

**Approx. ₹1.5--2 lakh**

-   Research-grade calibration
-   Higher-quality reference instrumentation
-   Model validation

These are target deployment tiers from the system blueprint and should
be treated as indicative estimates until a final BOM is prepared.

------------------------------------------------------------------------

## 15. Key Hazards

The architecture is designed around multi-hazard environmental
forecasting.

Potential hazard outputs include:

-   Flood
-   Landslide
-   Forest fire
-   Extreme heat
-   Air pollution
-   Water-quality events
-   Gas-related hazards
-   Drought / soil-moisture stress
-   Other site-specific environmental risks

The exact hazard-head definitions and validation status should be fixed
before final model deployment.

------------------------------------------------------------------------

## 16. Data & Cloud Pipeline

A conceptual cloud pipeline is:

``` text
Sensor / Node
     ↓
MQTT Ingestion
     ↓
Time-Series / Spatial Database
     ↓
Model Training
     ↓
Validation
     ↓
Model Registry
     ↓
Versioned Model
     ↓
Edge Deployment
```

The system can use:

-   MQTT for ingestion
-   Time-series / spatial databases
-   MLflow-style model registry
-   PostGIS-compatible spatial storage
-   Grafana-style visualisation

------------------------------------------------------------------------

## 17. Model Lifecycle

PRAHARI separates model development from field operation.

``` text
Historical Data
      ↓
Training
      ↓
Validation
      ↓
Calibration
      ↓
Versioned Model
      ↓
Edge Deployment
      ↓
Field Monitoring
      ↓
New Data
      ↓
Retraining
```

Model updates should be versioned and validated before deployment.

------------------------------------------------------------------------

## 18. Advantages

### Technical

-   Edge-first operation
-   Multi-hazard sensing
-   Distributed architecture
-   LoRa-based local networking
-   Graceful degradation
-   Local alert generation
-   Modular hardware

### Economic

-   Lower-cost distributed coverage
-   Scalable deployment
-   Reduced dependence on expensive monitoring stations
-   Replaceable/modular sensor pods

### Operational

-   Local warnings
-   Cloud-independent real-time alert path
-   Remote monitoring
-   Model updates
-   Fleet health monitoring

### Environmental

-   Continuous monitoring
-   Early hazard detection
-   Soil and water monitoring
-   Air-quality monitoring
-   Forest-fire monitoring

------------------------------------------------------------------------

## 19. Project Repository Structure

A suggested repository structure is:

``` text
PRAHARI/
│
├── README.md
│
├── hardware/
│   ├── schematic/
│   │   ├── head_node/
│   │   ├── sensor_pod/
│   │   └── power_supply/
│   │
│   ├── pcb/
│   │   ├── head_node/
│   │   ├── sensor_pod/
│   │   └── power/
│   │
│   ├── bom/
│   │   └── BOM.xlsx
│   │
│   └── datasheets/
│
├── firmware/
│   ├── head_node/
│   └── sensor_pod/
│
├── ai/
│   ├── training/
│   ├── models/
│   ├── quantization/
│   └── deployment/
│
├── cloud/
│   ├── ingestion/
│   ├── database/
│   ├── model_registry/
│   └── dashboard/
│
├── documentation/
│   ├── architecture/
│   ├── power/
│   ├── pcb/
│   └── deployment/
│
└── presentation/
    └── SIH_PRAHARI.pdf
```

------------------------------------------------------------------------

## 20. Prototype Development Roadmap

### Phase 1 --- Sensor Prototype

-   Integrate selected environmental sensors
-   Verify sensor readings
-   Implement basic data logging

### Phase 2 --- Communication

-   Implement RS-485 sensor communication
-   Implement LoRa communication
-   Test node-to-node communication

### Phase 3 --- Head Node

-   Integrate MCU / compute platform
-   Add GNSS
-   Add local storage
-   Add RTC
-   Add cellular connectivity

### Phase 4 --- Power

-   Integrate solar input
-   MPPT charging
-   LiFePO₄ battery
-   12 V distribution
-   5 V / 3.3 V regulated rails
-   Protection circuits

### Phase 5 --- Edge AI

-   Prepare sensor time-series data
-   Train temporal model
-   Add hazard-specific heads
-   Quantise for edge inference
-   Measure latency and power

### Phase 6 --- Field Validation

-   Deploy multiple nodes
-   Validate sensor calibration
-   Test LoRa range
-   Test connectivity failure
-   Test local alerts
-   Evaluate false alarms and missed events
-   Validate model performance using real field data

------------------------------------------------------------------------

## 21. Safety & Engineering Notes

This repository describes a prototype/system architecture. Before
hardware fabrication:

-   Verify every regulator's input/output range.
-   Calculate maximum and average current consumption.
-   Size fuses according to the actual wiring and load.
-   Verify battery protection/BMS requirements.
-   Verify solar-controller compatibility with the selected LiFePO₄
    battery.
-   Verify TVS and reverse-polarity protection ratings.
-   Perform thermal calculations for regulators.
-   Use appropriate creepage/clearance for any mains-connected
    circuitry.
-   Do not connect mains directly to low-voltage electronics.
-   Validate outdoor enclosure, grounding and surge protection.
-   Verify antenna placement and RF requirements.
-   Review all PCB traces and connector pinouts before manufacturing.

------------------------------------------------------------------------

## 22. Current Design Status

  Subsystem                   Status
  --------------------------- ---------------------------
  System architecture         Defined
  Sensor architecture         Defined
  Edge-AI concept             Defined
  LoRa mesh concept           Defined
  Power architecture          Defined
  Complete circuit concept    Prepared
  PCB concept                 Prepared
  Final component selection   To be verified
  Fabrication-ready PCB       Requires final validation
  Field validation            Required
  Production deployment       Future phase

------------------------------------------------------------------------

## 23. Key Specifications

  Specification             PRAHARI Target
  ------------------------- ----------------------------------------
  Solar input               30 W
  Battery                   LiFePO₄ 12.8 V, 20 Ah
  Main distribution         12 V
  Logic rails               5 V / 3.3 V
  Target autonomy           7 days without sunlight
  Local communication       LoRa
  Wired pod communication   RS-485
  Wide-area communication   GSM / NB-IoT
  Position / timing         GNSS
  Local storage             microSD / EEPROM
  Local alert               Siren / buzzer / relay
  AI approach               Shared temporal encoder + hazard heads
  Mesh scope                Local deployment mesh

------------------------------------------------------------------------

## 24. SIH Presentation

The project presentation is limited to six slides according to the
provided SIH template.

Recommended structure:

1.  **Title**
2.  **Proposed Solution**
3.  **Technical Approach**
4.  **Power & Hardware Implementation**
5.  **Feasibility, Viability & Impact**
6.  **Research & References**

The technical slide should contain the complete system/circuit
architecture, while the power slide should show the solar, MPPT,
battery, protection and regulated power rails along with the PCB
concept.

------------------------------------------------------------------------

## 25. Conclusion

PRAHARI proposes a distributed environmental intelligence architecture
where sensing, prediction and warning are moved closer to the community.

Its central design principle is:

> **Sense locally. Predict locally. Communicate intelligently. Alert
> locally. Learn globally.**

The combination of low-cost sensor pods, edge AI, LoRa networking, solar
power and local alerting is intended to provide resilient environmental
monitoring for communities where conventional monitoring infrastructure
is sparse or unreliable.

------------------------------------------------------------------------

## 26. Project Identity

**Project:** PRAHARI --- Environmental Intelligence Network\
**SIH:** Smart India Hackathon\
**Problem Statement:** SIH26178\
**Version:** 2.0\
**Category:** Hardware

------------------------------------------------------------------------

## License

Add the team's chosen license before publishing the repository publicly.
