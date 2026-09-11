# Code B — head node, MCU side (STM32U585 on the Arduino UNO Q)

Blueprint §6.1 responsibilities of the MCU: RS-485 bus master, LoRa
timing, power sequencing, watchdog, GNSS pulse capture — plus the §6.8
siren/relay *actuator*.  No hazard decision lives here; the Linux side
owns the model, the thresholds and the alert decision and sends
"sound the alert" over the bridge.

## Build

The repo has one Zephyr checkout, the west workspace under
`code_a_pod_firmware/`.  This app is freestanding and builds from there:

```sh
cd code_a_pod_firmware
west build -p always -b arduino_uno_q -d build/hn_mcu ../code_b_head_node/mcu
../code_b_head_node/mcu/tests/run_native.sh      # 5 ztest suites on native_sim
../code_b_head_node/mcu/tests/native/  # gcc-only unit + msgpack-interop tests, see that dir
```

`arduino_uno_q` is supported upstream in Zephyr 4.4 (SoC `stm32u585xx`);
no custom board definition was needed.  Image: 54.6 KB flash of 2 MB,
21.7 KB RAM of 768 KB.

## Layout

```
include/hn/   tdma.h pps.h bus_master.h supervisor.h rails.h alert.h hw.h
              mprpc.h bridge_contract.h    — the MsgPack-RPC bridge, see below
src/          pure modules of the same names + hw_*.c glue + main.c
boards/       arduino_uno_q.overlay — provisional carrier pinout (see file header)
tests/        tdma, bus_master, supervisor, pps, rails_alert_rpc (ztest, native_sim)
              native/               — gcc-only mprpc/bridge_contract unit + msgpack-interop tests
              native_bridge_sim/    — TEST-ONLY host simulator for the Linux-side integration test
```

The pod<->head wire protocol (frame format, CRC, report payload) is
defined once in Code A (`code_a_pod_firmware/app/{include/prahari/rs485.h,
wire.h, src/rs485.c, wire.c}`) and compiled into this image.  That is a
protocol dependency, not a dependency on pod behaviour.

## What each module does

| Module | Blueprint | Behaviour |
|---|---|---|
| bus_master | §5.2 | One sweep per cadence: POLL each address in order, wait `HN_BUS_REPLY_TIMEOUT_MS`, on silence mark missing and move on (worst case n × (timeout + turnaround)); wrong-address / late replies ignored; `$ARM` broadcast between sweeps |
| tdma | §7.3/§7.4, §4.4 | UTC-aligned superframes; mesh slot = FNV-1a(node id) mod n; layout: pod beacon, pod uplink slots, mesh slots; next-event arithmetic with guard |
| pps | §6.3 | PPS edges on a GPIO with the cycle counter; glitch rejection, missed-pulse tolerance, period EMA (drift ppm), UTC-of-second from the NMEA fix, holdover; local->UTC conversion |
| supervisor | — | IWDG fed only while main loop, bus sweep and TDMA loop each checked in inside their deadline |
| rails | §6.4 | Ordered bring-up (12 V mast bus, then radios) with settle delays; shed by priority as battery falls, restore with hysteresis |
| alert | §6.8 | Siren/relay actuator: warning = continuous, advisory = 1 s/2 s pattern, duration-capped, silence command |
| mprpc + bridge_contract | — | MessagePack-RPC codec + the actual MCU<->Linux message contract — see `../README.md` "The MCU <-> Linux message contract" for the full shape-by-shape reference and rationale |

Beacon and pod-uplink framing, and the LoRa PHY (866 MHz, SF9, 125 kHz),
match Code A's Mode R.  In each superframe this node sends the beacon,
listens through the pod region and the mesh region, and transmits the
embedding the Linux side last gave it (`set_embedding`) in its own mesh
slot.  Every decoded pod uplink is notified to Linux as `pod_report`;
every other received packet as `neighbour_packet`; aggregation is the
Linux side's job.

## Provisional pinout (UNO R3 header)

Documented in `boards/arduino_uno_q.overlay`.  Only one extra UART is
exposed on the header (USART3 on A1/A3), so GNSS takes it and RS-485
takes D0/D1 (USART1), leaving the internal LPUART1 (to the QRB2210) as
the ONLY UART for the bridge — dedicated to it exclusively, since it is
now a binary channel: there is no free UART left for a text console in
this configuration (see the overlay's header comment).  The real §10.1
carrier board will have its own pinout.

## Not done

- Vendoring the actual Arduino_RouterBridge C++ library: it needs the
  Arduino core (HardwareSerial/Stream) that this freestanding Zephyr app
  does not have.  `mprpc.c`/`bridge_contract.c` are a from-scratch,
  wire-compatible reimplementation instead — see `../README.md` for the
  full justification and the interop tests that back the claim.
- Adaptive SF (§7.3): fixed per node by Kconfig.
- Downlink to Mode R pods (arm) on the beacon.
