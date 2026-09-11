# `test_env/l0_stimulus/` — L0 physical stimulus generator

Produces **electrical-level** stimulus that Renode feeds into Code A's
*actual* peripheral drivers. It does **not** produce synthetic sensor
readings that bypass Code A: the point is that Code A's real
acquisition → two-point calibration → median-of-5 spike rejection →
range-gate → z-score anomaly gate → instantaneous derivations pipeline
runs against this stimulus exactly as it would against real hardware.

## Not the same thing as the training-pipeline data generator

`code_b_head_node/training_pipeline/prahari_train/synthetic.py` also
produces synthetic data, but at a completely different level:

| | this generator (`test_env/l0_stimulus/`) | `training_pipeline/…/synthetic.py` |
|---|---|---|
| Output | ADC counts/µV, GPIO edge trains, I²C response bytes, UART frames | application-level parameter time-series (P1…, S1…, T1…) |
| Consumer | Renode → Code A's driver ISRs and the §4.3 pipeline | the offline model training / ONNX export, never a device |
| Runs where | the L0–L5 test harness | a dev machine, offline |
| Purpose | exercise the pod firmware's real acquisition path | fit / calibrate the model with no real data yet |

They are both synthetic and both stand in for missing real data, but one
is *below* Code A (electrical) and the other is *above* Code B (features).
Keep them separate; do not import one from the other.

## Physical scenario — blueprint §11 village worked example

`physical.py` models the §11.7 narrative as SI trajectories over five
days: a few pre-monsoon showers that each wet the soil and drain back
partway, then a sustained heavy burst on day four that

- drives the shallow soil (S1, −10 cm) from ~0.16 to saturation (~0.46 m³/m³),
- lifts pore-water pressure at the slope toe from 6 to ~63 kPa,
- starts the mast tilting (a few tens of mg of drift on X/Y),
- sends the river stage up at ~8 cm/h on the rising limb before it
  crests near 1.2 m and recedes over ~a day,
- cools and humidifies the air under the rain, drops solar, washes out
  particulate, and puts a shallow synoptic pressure fall ahead of the burst.

Everything is deterministic given `VillageScenario.seed`.

## What a bundle contains (`bundle.py`, one directory per pod position)

```
eeprom.bin            pod config image: position block, RS-485/LoRa identity,
                      and a two-point calibration table that is the exact
                      inverse of this bundle's ADC transfer functions, so
                      Code A turns raw millivolts back into the parameter
                      milli-unit and the value lands in the overlay range gate
adc/<name>.csv        timestamp_ns,voltage(µV)  — csv2resd input (VOLTAGE block)
adc/<name>.count.csv  timestamp_ns,count(0..4095) — STM32_ADC.FeedSample form
adc/vrefint.count.csv channel 17: the pod's own-rail (§4.3) self-report
pulse/<name>.json     GPIO edge train: port, pin, idle level, [[t_ns, level]…]
i2c/<name>.jsonl      per acquisition cycle: {t_ns, addr, response_hex, decoded}
uart/<name>.bin       concatenated PMS7003 frames  (+ .jsonl with decode)
manifest.json         scenario, dt, channel map, and the electrical envelope
                      every artifact was checked against
resd/                 (with --resd) the above as Renode .resd via tools/csv2resd
stimulus.resc         skeleton showing how the artifacts attach to peripherals
```

The overlay this matches is
`code_a_pod_firmware/app/boards/nucleo_l053r8.overlay`. Channel numbers,
GPIO pins, I²C addresses and range-gate bounds are transcribed from it and
from the Zephyr driver sources (`sht4x`, `lps22hb`, `lis2dh`, `pms7003`,
`stm32_vref`), not invented.

### Transducer classes (blueprint §3.1)

- **Class C — ADC** (`xdcr_adc.c`): soil moisture ×4 (capacitive probe,
  2.6 V dry → 1.0 V saturated), pore pressure (piezometer through a
  divider, 0.33–3.0 V), MOX gas, pyranometer, potentiometric wind vane.
  Each has a matched calibration entry in `eeprom.bin`.
- **Class D — pulse** (`xdcr_pulse.c`): tipping rain gauge (0.2 mm/tip,
  active-low, 10 ms debounce, with optional contact chatter) and cup
  anemometer (1 pulse/rev, 2 ms debounce). Emitted as GPIO edge lists.
- **Class A — I²C** (`xdcr_sensor.c`): SHT4x air T/RH, LPS22HB pressure,
  LIS2DH mast tilt — the exact register-read response bytes, with the
  CRC-8 (poly 0x31) the SHT4x driver verifies.
- **Class B — UART** (`xdcr_sensor.c`): PMS7003 32-byte frames at 9600 baud.

## Gaps in the current overlay (flagged, not worked around)

`nucleo_l053r8.overlay` does not populate every transducer the §11.4
village column implies, so the stimulus for those channels has nowhere to
go until the overlay (or the real L051 board) adds them:

- **No P11 water-level / P38 turbidity transducer** anywhere. `physical.py`
  still models river stage (it drives the scenario and the pore-pressure /
  soil coupling), and `manifest.json` records it under
  `unrealised_channels`, but there is no ADC channel to feed it into — the
  flash-flood end-to-end path is only partial with this overlay.
- The overlay puts `imu_tilt` and `pore_pressure` at position **G**;
  §11.4 puts them on the R3 offset slope pod. This generator follows the
  overlay (tilt + pore at G).
- The overlay already flags, and this generator inherits, the wind-vane
  class C (analogue) vs §3.1 class D (pulse) conflict — modelled as
  analogue here.

## Usage

```sh
python -m test_env.l0_stimulus.generate --out /tmp/l0 --dt 60
python -m test_env.l0_stimulus.generate --out /tmp/l0 --resd   # + Renode .resd
python -m test_env.l0_stimulus.generate --out /tmp/l0 --position G --position U
../../.venv-ml/bin/python -m pytest test_env/l0_stimulus/tests -q
```

`--dt` defaults to 60 s (Code A's `CONFIG_PRAHARI_SAMPLE_PERIOD_S`); use a
finer grid to give Renode's sample-and-hold more resolution.

## Renode model availability (checked, not assumed)

Checked against Renode 1.17 (`~/renode_portable`):

- `STM32_ADC` exposes `FeedSample(value, channelIdx, repeat)` — **raw
  12-bit counts**, so `adc/<name>.count.csv` is the primary ADC artifact
  (schedule one `FeedSample` per row). It does **not** expose
  `FeedSamplesFromRESD`; the `adc/<name>.csv` (µV) and `resd/adc_*.resd`
  are kept for RESD-capable ADC models and future use.
- `STM32_GPIOPort` exposes `OnGPIO(pin, value)`. There is no built-in
  frequency-drive, so the anemometer is emitted as a **rate-interval
  schedule** (`pulse/anemometer.json`, Hz per acquisition window, capped
  at 500 Hz so 1/f stays above the 2 ms debounce) rather than millions of
  enumerated edges; the sparse rain gauge is emitted as an explicit edge
  train with contact chatter.
- A UART RESD feeder (`FeedDataFromRESD`) and `tools/csv2resd` are present;
  `uart/pms7003.bin` (raw) and `resd/uart_pms7003.resd` are both produced.
- Renode bundles **no** exact model for the SHT4x, LPS22HB or LIS2DH, and
  **no** L051/L053 `.repl` (only `stm32l071` / `stm32l072`). Wiring those
  in — a scripted I²C responder fed from the `i2c/*.jsonl` frames here, and
  a `.repl` verified against RM0451 — is `test_env/renode/`'s job; this
  package only produces the stimulus and documents the contract.

## Electrical validity is tested

`tests/` asserts every generated artifact stays inside what real hardware
could produce: ADC counts in `[0, 4095]` and voltages in `[0, 3.3] V`,
GPIO levels in `{0, 1}` with edges never closer than the reed-switch
floor and each pulse an even number of transitions back to idle, I²C/UART
bytes in `[0, 255]` with correct CRCs and decoded values inside each
sensor's representable range, and the `eeprom.bin` calibration inverting
the ADC transfer functions to within a milli-unit — so the soil-moisture
value Code A computes lands inside the overlay's range gate.
