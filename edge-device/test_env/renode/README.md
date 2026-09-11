# `test_env/renode/` — village-column emulation harness (L1 pods + L3 head-node MCU)

`village_column.resc` builds four Renode machines — three wired pods
(S1 / G / U, blueprint §11.4) and one head-node MCU — loads the **actual
compiled firmware** onto each, wires them on an RS-485 Mode W multi-drop
bus (§4.4), feeds each pod the L0 electrical stimulus from
`test_env/l0_stimulus/`, and exposes the head node's MessagePack-RPC
bridge UART as a raw TCP socket for Code B's Linux side.

## What is real

| Piece | Source | Status |
|---|---|---|
| Pod firmware | `code_a_pod_firmware/build/pod_l053_w/zephyr/zephyr.elf` — Code A, Mode W, one image, position per EEPROM | **real ELF, boots** |
| Head-node MCU firmware | `code_a_pod_firmware/build/hn_mcu/zephyr/zephyr.elf` — Code B MCU side (arduino_uno_q / STM32U585) | **real ELF, boots** |
| Pod platform | `platforms/stm32l053.repl` — hand-verified vs RM0377 + Zephyr l0 dtsi (see `platforms/README.md`) | verified, boots the real ELF |
| Head-node platform | `platforms/stm32u585.repl` — hand-built vs RM0456 + stm32u5.dtsi (see `platforms/README.md`) | verified, boots the real ELF |
| EEPROM per pod | `stimulus/pod_<POS>/eeprom.bin` from `test_env/l0_stimulus/` — position + RS-485 addr + calibration table | **loaded and read by the firmware** |
| RS-485 bus | `emulation CreateUARTHub "rs485bus"` + `connector Connect sysbus.usart1 rs485bus` on all four machines | wired |
| Stimulus | `stimulus/pod_<POS>/adc/*.count.csv` counts via `sysbus.adc1 FeedSample`; rain-gauge GPIO edge | wired |
| MCU↔Linux bridge | `emulation CreateServerSocketTerminal 3390 "bridge" false` (raw, no telnet IAC) on `sysbus.lpuart1` | socket opens |
| Linux side | `linux_bridge_probe.py` — connects to tcp:3390, decodes `pod_report` frames with the shipping `prahari_hn.wire.decode_report` | ready |

`village_column.resc` aborts (Python `assert`) if either ELF or the
stimulus bundle is missing — it never substitutes a stand-in.

## What works, verified

Running a single pod machine from `platforms/stm32l053.repl` with its
`eeprom.bin`, the real firmware:

```
*** Booting Zephyr OS build ... ***
PRAHARI pod firmware, position G (eeprom)         <- position resolved from the EEPROM image
4 of 15 transducers populated at G:
  surface_soil     C/ADC    rail=soil-excitation init=0
  pore_pressure    C/ADC    rail=analogue-aux init=0
  rain_gauge       D/pulse  rail=passive init=0
  imu_tilt         A/I2C    rail=sensor-rail init=-19   <- no LIS2DH model in Renode; channel masked (correct)
calibration: 2 entries from eeprom                <- calibration table read from the EEPROM image
link: W/RS-485 addr=2 slot=0 (eeprom)             <- RS-485 Mode W link identity from the EEPROM image
```

The head-node U585 ELF boots and executes (~1e8 instructions/s) on
`platforms/stm32u585.repl`.

## Where it stops — and why this is a "stop and say so", not a workaround

After `link: W/RS-485 ...` the pod firmware does **not** reach its first
acquisition cycle: no `report N: P10=... ` line is ever printed, across
virtual time that contains many sample periods.

Root cause: **the Zephyr kernel tick does not advance under any Renode
1.17 platform available for these chips.** Renode bundles no
`nucleo_l053r8` / `stm32u585` platform; the `dts2repl`-generated
platforms and the nearest bundled family (`stm32l071`) boot the firmware,
but their `IRQControllers.NVIC` counts SysTick down (CSR = 0x7, CVR
decrements) without delivering the SysTick exception to the core. Zephyr's
`sys_clock` therefore never advances, `k_uptime_get` is frozen, and every
`k_sleep` / timeout in Code A's main loop either busy-waits or blocks — so
the pipeline (calibration → median-of-5 → range-gate → derivations) is
never entered and nothing is produced to cross the RS-485 bus or the
bridge socket.

This is not something a `.repl` edit fixes (the SysTick registers are the
Cortex-M core's, implemented by Renode's NVIC, not a peripheral). The
prerequisites are a real `nucleo_l053r8` / `stm32u585` Renode platform, or
a SysTick-exception-delivery fix in Renode. Per the task's instruction —
if the firmware won't run, stop and say so rather than substitute
something else — the harness is delivered complete and correct up to that
wall; it does not fake the processed value or the bridge traffic.

The bundled Antmicro `stm32l072` Zephyr-shell demo shows the same
instruction-flatline under this Renode build; it only *appears* to work
because a shell that waits on UART RX is not time-dependent.

## Running it

```sh
# 1. build both firmwares (once)
cd code_a_pod_firmware
west build -p always -b nucleo_l053r8 -d build/pod_l053_w app -- -DCONFIG_PRAHARI_LINK_MODE_W=y
west build -p always -b arduino_uno_q -d build/hn_mcu ../code_b_head_node/mcu

# 2. generate the L0 stimulus bundle
cd ..
.venv-ml/bin/python -m test_env.l0_stimulus.generate --out test_env/renode/stimulus

# 3. regenerate the platforms if a build's devicetree changed
# platforms/*.repl are hand-maintained (see platforms/README.md); regenerate only if a
# peripheral address changes, and re-verify against the RM as that README documents.

# 4. run headless
renode --disable-xwt --console --plain \
  -e 'include @test_env/renode/village_column.resc' \
  -e 'emulation RunFor "90"' -e 'quit'

# 5. Code B Linux side, in another shell
.venv-ml/bin/python test_env/renode/linux_bridge_probe.py 127.0.0.1 3390 --expect-param 10
```
