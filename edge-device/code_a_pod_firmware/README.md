# Code A — pod firmware (L1) — Zephyr workspace

This directory is a **west workspace** (topology T2). The application and
its manifest live in `app/`; everything else here is fetched by west and
is not project source.

```
code_a_pod_firmware/
  app/            the pod firmware application + west.yml   (committed)
    Kconfig, prj.conf, CMakeLists.txt
    boards/<board>.overlay   load switches + transducer population per board
    dts/bindings/            prahari,load-switch / prahari,*-transducer
    include/prahari/         params.h (P-IDs), pod_position, xdcr, power, pulse, sampler
    src/                     pure modules + pod_dt_tables.c (DT -> tables) + main.c
    src/drivers/             class drivers: xdcr_adc (C), xdcr_pulse (D), xdcr_sensor (A/B)
    tests/                   ztest suites (native_sim) + run_native.sh
  zephyr/         Zephyr RTOS v4.4.2, shallow clone          (west update)
  modules/hal/    cmsis, cmsis_6, hal_stm32                  (west update)
  build/          build output                               (ignored)
  .west/          workspace marker                            (ignored)
```

## How the one image serves six positions

Every transducer any position can carry is a node under `/transducers`
in the board overlay, with `prahari,params` (P-IDs from
`prahari_parameters.md`), `prahari,positions` (which of S1 S2 S3 G U C
populate it) and `power-domain` (a `/load-switches` child carrying the
§3.4 policy, current, settle/on/period).  `src/pod_dt_tables.c` turns
those nodes into `pod_xdcrs[]` and `pod_pwr_domains[]` at compile time;
at boot the pod resolves its position (EEPROM block, else the Kconfig
default) and enables only the matching nodes.  Adding a transducer to a
position = adding or editing a devicetree node.

Load-switch policies: `per-read` (on around each read), `periodic`
(own duty cycle, e.g. fan 30 s every 15 min), `armed` (MOX heater),
`hold`.  The rain gauge has no power-domain: it is the one passive
transducer (§3.4).

## §4.3 processing pipeline (app/src/pipeline.c)

Per frame, per in-situ channel: two-point calibration (coefficients in
EEPROM at `CONFIG_PRAHARI_EEPROM_CALIB_OFFSET`, format in
`include/prahari/calib.h`) -> median-of-5 spike rejection -> range gate
(`prahari,range-lo/hi` in the overlay; out-of-spec is FLAGGED, value kept)
-> z-score gate against a `CONFIG_PRAHARI_ZGATE_WINDOW`-sample rolling
window (raises the pod wake flag) -> instantaneous derivations S13 VPD,
S14 dew point, S15/S16 wind u/v (sin/cos, never a bearing), S21 heat index
-> self-report (rail mV from the STM32 `vref` channel, per-transducer
fault / no-power / range bits).

Unit contract: every value is an int32 in milli-units of the parameter's
unit from prahari_parameters.md.  A raw ADC/pulse channel with no
calibration entry keeps its raw value and is marked UNCAL: it is never
filtered, gated or used in a derivation.

The z-gate window is the only history this image keeps.  Multi-day
indices and the fire-weather cascade are Code B's.

## Head-node link (blueprint §4.4): Mode W or Mode R, never both

`CONFIG_PRAHARI_LINK_MODE_W` (default) or `CONFIG_PRAHARI_LINK_MODE_R`
picks exactly one link stack at build time.

- **Mode W** (`src/link_w.c`, `src/rs485.c`): RS-485 half-duplex
  multi-drop, pod = polled slave.  `/rs485` node (`prahari,rs485`) names
  the UART and DE pin.  Frame `7E addr cmd len payload crc16`; commands
  POLL -> REPORT, ARM -> ACK, PING -> PONG, unknown -> NAK.  The pod
  answers only unicast frames to its own address (§5.2 sweep); it never
  answers a broadcast POLL or another pod's reply, and needs no logic to
  notice it was skipped.  DE is held until the last byte has shifted out.
- **Mode R** (`src/link_r.c`, `src/lora_slot.c`): LoRa SX1262 through
  Zephyr's native SX126x driver (chosen `prahari,lora`), 866 MHz /
  SF9 / 125 kHz by Kconfig.  Head-node beacon `'P''B'` marks each
  superframe; the pod opens a short receive window for it, re-syncs, and
  transmits `'P''R' addr report crc16` at
  `superframe + beacon_ms + slot * slot_ms`.  Without a beacon it
  free-runs (holdover) and keeps reporting.
- Shared wire format (`src/wire.c`): report = 13 B header + per-channel
  `{id, flags, int32}` for every populated P-channel (masked ones travel
  with their reason flag) + derived S-channels.  Full pod: 117 B, one
  LoRa packet.  CRC-16/CCITT-FALSE throughout.
- Identity (`src/link_cfg.c`): EEPROM block `'L''K' ver addr slot crc8`
  at offset 8, else `CONFIG_PRAHARI_LINK_ADDR/SLOT`.

EEPROM map: 0 position block (5 B) · 8 link block (6 B) · 16 calibration table.

Dev-board pins: on the Nucleo-32 L031K6, RS-485 shares USART2 with the
console (build hardware Mode W with `-DCONFIG_UART_CONSOLE=n`), and Mode R
needs `-DEXTRA_DTC_OVERLAY_FILE=boards/nucleo_l031k6_mode_r.overlay`,
which frees SPI1/PA12/PB0/PB1 by disabling five ADC transducers — a
32-pin dev-board limitation, not a design one.  The L053R8 stand-in has
both links wired without compromise.

## Tests

```sh
./app/tests/run_native.sh        # builds + runs the 6 suites on native_sim/native/64
# or, through twister (needs: pip install -r zephyr/scripts/requirements-run-test.txt junitparser pytest):
ZEPHYR_BASE=$PWD/zephyr python3 zephyr/scripts/twister -p native_sim/native/64 -T app/tests -O build/twister
```
(`west twister` itself crashes in west 1.5's error handler under Python 3.14
when an import fails; invoking the script directly reports the real error.)

| Suite | Covers |
|---|---|
| pod_position | EEPROM block validation, Kconfig fallback, CRC/magic/version/range |
| xdcr_select | position -> transducer set, one passive transducer, same part / different P-IDs by position |
| power | §3.4 duty table as a state machine: per-read, periodic, armed, hold, caps, budget, wrap |
| sampler | rail on only around a read, rain gauge never switched, masked-not-imputed, fan read in its window, pulse debounce |
| link | RS-485 framing/CRC/resync, address matching (own, other, master, broadcast), slot timing from a beacon incl. guard/wrap/holdover, wire roundtrip, time-on-air, EEPROM link block |
| pipeline | calibration exactness + EEPROM roundtrip/CRC, single spike rejected by median vs sustained step tripping the z gate, range-gated values flagged but returned, wind u/v continuity across 359->0, VPD/dew point/heat index reference values |

## Image size (Cortex-M0+, size-optimised, console on)

| Build | FLASH | RAM |
|---|---|---|
| hello_world on L031K6 | 13668 B of 32 KB (42%) | 2112 B |
| driver layer only, L031K6 | 29788 B of 32 KB (91%) | 3704 B |
| + §4.3 pipeline + Mode W, L031K6 | **overflows by 12432 B** | — |
| + §4.3 pipeline + Mode R, L031K6 | **overflows by 21972 B** | — |
| + §4.3 pipeline + Mode W, L053R8 stand-in (64 KB) | 45976 B (70%) | 5928 B of 8 KB (72%) |
| + §4.3 pipeline + Mode R, L053R8 stand-in (64 KB) | 55832 B (85%) | 7080 B of 8 KB (86%) |

The STM32L031 cannot hold the pod firmware once the §4.3 pipeline is in;
the soft-float derivations and libm alone are several KB on a core with no
FPU.  `boards/nucleo_l053r8.overlay` exists ONLY to measure the image on a
64 KB L0 part.  The real target is the L051 (blueprint §4.2), which needs
a custom board definition (SoC support exists in Zephyr).  **RAM is now the
binding constraint for Mode R**: the L051 also has only 8 KB, and the
SX126x driver + SPI + link buffers take the image to 86 %.

## Versions (set up 2026-09-09)

| Component | Version | Why |
|---|---|---|
| Zephyr | v4.4.2 (tag, pinned in `app/west.yml`) | Newest stable release; 4.4 opened the six-month cadence and is the LTS candidate |
| Zephyr SDK | 1.0.1 at `~/zephyr-sdk-1.0.1` | The version `zephyr/SDK_VERSION` pins for v4.4.2 |
| Toolchain | arm-zephyr-eabi, GCC 14.3.0, binutils 2.43.1 | From the SDK; the only Zephyr-supported toolchain |
| Host tools | dtc 1.7.0, openocd, qemu (SDK hosttools) | Installed by `setup.sh -h`; dtc is also used by the build |
| west | 1.5.0 | in `../.venv-zephyr` |
| CMake | 4.2.3 (system) | |
| ninja | 1.13 (pip, in `../.venv-zephyr`) | no sudo on this host, so not from apt |
| Python | 3.14.4 | `../.venv-zephyr`, with `zephyr/scripts/requirements-base.txt` |

Not installed: `gperf` (only needed for `CONFIG_USERSPACE`, which the
pod does not use) and `ccache` (optional).

## Target

Blueprint §4.2 names **STM32L031 / STM32L051**. Zephyr v4.4.2 has SoC
support for both (`SOC_STM32L031XX`, `SOC_STM32L051XX`) but an in-tree
board only for the L031:

- `nucleo_l031k6` — STM32L031K6, 32 KB flash / 8 KB RAM. Exact SoC match.
  Used for smoke builds and as the reference for the custom pod board.
- There is **no** L051 board. `nucleo_l053r8` is the nearest board but is
  a different SoC (L053, adds USB/LCD). An L051 target needs a custom
  board definition (see the session notes / next steps).

## Build

```sh
source ../.venv-zephyr/bin/activate          # from code_a_pod_firmware/
west build -p always -b nucleo_l031k6 -d build/smoke_blinky zephyr/samples/basic/blinky
```

The SDK is found through the CMake user package registry
(`~/.cmake/packages/Zephyr-sdk`), so no `ZEPHYR_SDK_INSTALL_DIR` or
`ZEPHYR_TOOLCHAIN_VARIANT` export is needed.

## Reproducing the workspace from scratch

```sh
cd code_a_pod_firmware
../.venv-zephyr/bin/west init -l app
../.venv-zephyr/bin/west update --narrow -o=--depth=1
../.venv-zephyr/bin/pip install -r zephyr/scripts/requirements-base.txt
```

Host note: the SDK's `setup.sh` uses `wget`, which hung on this network
against GitHub's release-asset host. Workaround: download the toolchain
tarball with `curl`, extract it into `~/zephyr-sdk-1.0.1/gnu/`, then run
`./setup.sh -t arm-zephyr-eabi -h -c` (it skips an already-extracted
toolchain).
