# Code B — head node (blueprint §6)

Two cooperating parts on one Arduino UNO Q board:

- **`mcu/`** — STM32U585, Zephyr. RS-485 bus master, LoRa TDMA, power
  sequencing, watchdog, GNSS pulse capture, the siren/relay actuator.
- **`linux/`** — QRB2210, Debian. ONNX Runtime inference, mesh embedding
  exchange, uplink, SQLite ring buffer, alert decisioning, duty cycling.

They talk over the board's native MPU<->MCU bridge. This document is the
contract between them: the message shapes, who initiates each one, and
the timing a caller should expect — the one place both sides' authors
(and both codebases' tests) have to agree without either side being able
to infer the other's assumptions from context. The two independent
implementations of it are `mcu/include/hn/bridge_contract.h`
(shapes) + `mcu/src/bridge_contract.c` (encode/decode) on the MCU side,
and `linux/prahari_hn/bridge.py` on the Linux side.

## The real bridge, and why part of it is a reimplementation

The UNO Q's native bridge is not a direct MCU<->Linux link. A **Router**
process (`arduino-router`, part of the board's Debian image) sits in the
middle:

```
MCU (Zephyr)  --UART, MessagePack-RPC-->  Router  <--Unix socket, MessagePack-RPC--  Linux (Debian)
   Arduino_RouterBridge (C++)                        arduino_router_bridge (Python, official)
```

Both sides `provide()` methods (make them callable by whichever peer asks
the Router for them by name), `call()` them (blocking, expects a
response), and `notify()` them (fire-and-forget) — symmetrically. The
Router's only job is routing by registered method name; it never
inspects or acts on payloads (nothing in the decision path is allowed to
run there, matching the mesh/cloud boundary at every other layer of this
system).

**Linux side**: this is the real, official `arduino-router-bridge` PyPI
package (`arduino.router_bridge.Bridge`), talking real MessagePack-RPC.
`linux/prahari_hn/bridge.py` is a thin wrapper around it — nothing about
the protocol is reimplemented there.

**MCU side**: the vendor's `Arduino_RouterBridge` library is C++ that
requires the Arduino core (`HardwareSerial`, `Stream`) layered on top of
Zephyr. `mcu/`'s app is a **freestanding** Zephyr app (no Arduino core —
see `mcu/README.md`), so that library cannot be linked in as-is.
Per CLAUDE.md's rule against silently substituting a dependency:
`mcu/include/hn/mprpc.h` + `mcu/src/mprpc.c` are a from-scratch
MessagePack encoder/decoder, and `mcu/include/hn/bridge_contract.h` +
`mcu/src/bridge_contract.c` implement this contract's actual message
shapes on top of it. This is **not** a re-invented transport — UART +
MessagePack-RPC, self-delimiting with no extra framing, is exactly what
`Arduino_RPClite`'s `SerialTransport` already does — it is a
protocol-*compatible* reimplementation, verified to interoperate
byte-for-byte with the real `msgpack` Python package in both directions
(`mcu/tests/native/test_native.c --dump-interop`, cross-checked in
`mcu/tests/native/` against the real library; see "How the integration
test runs" below for the fuller proof).

## The MCU <-> Linux message contract

All values are physical/decoded, never raw analogue counts — Code A
already calibrates on the pod. Every message name below is a literal
constant in `bridge_contract.h` (`BC_METHOD_*`); a typo on either side is
a silent contract break, not a build error, which is exactly why
`linux/tests/test_bridge_integration.py` exercises the real encoder on
one side against the real decoder on the other, not each side's idea of
what the other expects.

### Sensor readings and pod health: MCU → Linux

All six are **notifications** (fire-and-forget) — Linux never acks them,
and the MCU never waits for one. This matches the physical reality: pod
telemetry is one-way, and blocking the RS-485 sweep on a Linux
acknowledgement would make the sweep's timing depend on Linux being
awake, which the risk-adaptive duty cycle explicitly cannot assume.

| Method | Params | Sent when | Notes |
|---|---|---|---|
| `pod_report` | `addr: int, report: bin` | Every pod that replies in a sweep, and every decoded Mode R (LoRa) uplink | `report` is Code A's wire-format v1 payload, unmodified — the one permitted Code B→Code A dependency (CLAUDE.md) |
| `pod_missing` | `addr: int` | A polled pod is silent for the sweep | Its channels are simply absent this cycle; masked, never imputed, on the Linux side |
| `sweep_done` | `sweep: int, present: int, total: int` | Once per RS-485 sweep (§5.2) | **The natural trigger for one inference cycle** — `node_status` is sent immediately after it, every time |
| `neighbour_packet` | `packet: bin` | Any received LoRa packet that is not a Mode R pod uplink | Raw bytes; decoding a mesh embedding packet (§7.5, 20 bytes) is the Linux side's job |
| `time_sync` | `utc_ms: int, state: str` | Once per TDMA superframe | `state` is `"locked"` / `"holdover"` / `"unlocked"` (§6.3 GNSS discipline) |
| `node_status` | one map: `sweeps, pods, pps, drift_ppm, batt_mv` | Alongside every `sweep_done` | Periodic health push — Linux does not need to poll for this in normal operation |

### Alert-trigger and duty-cycle-change commands: Linux → MCU

All five are **notifications** too — reliability comes from state being
re-asserted, not from an ack. `set_cadence`/`set_pods`/`set_armed` set
standing state the MCU keeps applying every cycle regardless of whether
one particular datagram was lost; `set_alert` is re-sent by the alert
engine on every level change (see `linux/prahari_hn/alerts.py`), so a
single dropped notification self-heals on the next state change rather
than needing its own retry logic.

| Method | Params | Meaning | Blueprint |
|---|---|---|---|
| `set_alert` | `level: int (0/1/2), seconds: int` | Siren command: off / advisory (intermittent) / warning (continuous), capped at `seconds` | §6.8 |
| `set_cadence` | `seconds: int` | RS-485 sweep + inference cadence — hourly baseline, 5-minutely once any head is at warning | §6.4 |
| `set_pods` | `addrs: array of int` | RS-485 poll list | §5.2 |
| `set_armed` | `armed: bool` | MOX gas-sensor heaters on all pods (broadcast) | §3.4 |
| `set_embedding` | `emb: bin` | This node's 16-byte int8 embedding, for the MCU's next mesh TDMA slot | §7.5 |

### The one call/response pair: `get_status`

Linux **calls** `get_status()` (blocking, MCU-provided) when it wants a
synchronous read instead of waiting for the next periodic `node_status`
push — e.g. on startup, or to confirm liveness before deciding whether
the uplink should report the node as degraded. It returns the identical
map shape as `node_status`. This is the only request/response pair in the
contract; everything else is one-way by design, per the reasoning above.

### Timing expectations

- **Cadence**: driven entirely by `set_cadence` — hourly at baseline,
  every 5 minutes once any hazard head is at warning (§6.4). The MCU does
  not decide this; it only applies whatever value Linux last sent.
- **Sweep timing**: `sweep_done` arrives once per sweep, worst case
  `n_pods × (HN_BUS_REPLY_TIMEOUT_MS + HN_BUS_TURNAROUND_MS)` after the
  sweep starts (`mcu/README.md`'s bus_master row).
- **`set_alert`/`set_cadence`/etc. are not queued** — a notification sent
  while the MCU is mid-sweep is simply processed at the next
  `poll_bridge()` call in the 10 ms supervisory loop (`mcu/src/main.c`),
  i.e. within about one loop iteration, never blocking the sweep itself.
- **No message in this contract is retried by the transport.** A
  notification that never arrives (link drop, reboot mid-flight) is
  silently lost at that layer; each side's own state (Linux's alert
  engine re-sending `set_alert` on every level change, the MCU re-sending
  `node_status` every sweep) is what makes that acceptable, not an
  acknowledgement protocol here.

## How the integration test runs

`linux/tests/test_bridge_integration.py` runs both real implementations
against each other:

```
prahari_hn.bridge.HeadNodeBridge  <--tcp://-->  MockRouter  <--tcp://-->  native_bridge_sim (sim_main)
(real arduino_router_bridge,                    (test-only)              (mcu/src/mprpc.c +
 the code that ships)                                                     mcu/src/bridge_contract.c,
                                                                           the code that ships —
                                                                           only the transport and
                                                                           main() are test-only)
```

- **`linux/tests/mock_router.py`** stands in for the real `arduino-router`
  daemon (which isn't installable off the board). It implements the same
  routing a real router does — `$/register`/`$/unregister`, forwarding
  calls with per-connection msgid remapping, forwarding notifications,
  erroring an unregistered method — over the officially-documented
  `tcp://host:port` development address (`arduino.router_bridge.transport`
  refuses `unix://` off the real board *by design*, so `tcp://` is not a
  workaround, it's the sanctioned dev path). It is test-only and never
  runs on the board.
- **`mcu/tests/native_bridge_sim/sim_main.c`** links the actual
  `mcu/src/mprpc.c` and `mcu/src/bridge_contract.c` — the same object code
  the Zephyr firmware ships — behind a plain POSIX TCP client standing in
  for the UART, and a small stdin command language (`POD`, `MISSING`,
  `SWEEP`, `NBR`, `TIME`, `NODESTATUS`, `DUMP`, `SETPPS`, `QUIT`) that is
  test-only control surface, not part of the contract. `mcu/README.md`
  and that file's own header comment repeat this distinction.
- Before either the C encoder or decoder is trusted at all,
  `mcu/tests/native/test_native.c` round-trips every message shape
  against itself, checks fixstr/str8, bin8/bin16 and fixarray/array16
  boundaries, and — the actual point — dumps every MCU→Linux message and
  feeds it through the **real** `msgpack` Python package
  (`msgpack.unpackb`), and separately feeds messages **encoded by** that
  real package into the C decoder, both directions checked byte-for-byte.
  `mcu/tests/rails_alert_rpc/` repeats the core of this as a Zephyr
  `ztest` suite on `native_sim`, and the real firmware for `arduino_uno_q`
  is built and confirmed to still fit (54.6 KB flash, 21.7 KB RAM).

Run it:

```sh
cd code_b_head_node/linux
../../.venv-ml/bin/python -m pytest tests/test_bridge_integration.py -v
```

## Known gap: `linux/prahari_hn/node.py` is not yet on this bridge

`node.py` (the head-node orchestration loop: window building, inference,
alert decisioning, duty cycling, uplink) predates this contract and still
talks the old ad hoc `$`-line protocol via `linux/prahari_hn/rpc.py`,
which `mcu/`'s firmware no longer speaks — `mcu/src/rpc.c` and
`hn/rpc.h` were removed when `bridge_contract.c` replaced them. Porting
`node.py` onto `prahari_hn.bridge.HeadNodeBridge` (event-queue polling
instead of a line-parsing loop, and re-validating `linux/tests/test_node.py`
against it) is real, separate follow-up work, flagged here rather than
silently left for someone to discover as a mismatch — see CLAUDE.md
"Flag source-document conflicts, don't resolve them silently," which
applies just as much to a conflict this session's own changes introduced.
