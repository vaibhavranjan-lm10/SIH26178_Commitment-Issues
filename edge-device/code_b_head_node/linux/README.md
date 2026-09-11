# Code B — head node, Linux side (QRB2210 on the Arduino UNO Q)

The runtime that ships on the board's Debian side.  Python package
`prahari_hn`, runtime dependencies **numpy** and **onnxruntime** (aarch64,
CPU execution provider).  It never trains anything and never imports the
offline training pipeline; the model reaches it only as the versioned ONNX
artifact described in `training_pipeline/README.md`.

## Execution order per cycle (blueprint §6.7)

| Stage | Module | What happens |
|---|---|---|
| 0 | `rpc.py`, `wire.py` | `$POD` lines from the MCU (which polled Code A pods over RS-485 / heard them over LoRa) are decoded (Code A wire v1, milli-units) and stored per sweep; a `$MISSING` pod's channels are simply absent |
| 1 | `store.py`, `features.py` | hourly grid of the last 21 d + 30 d history from the ring buffer; the 41 trained channels (secondary/tertiary recomputed exactly as the training reference); **mask, never impute** — the normalisation itself is inside the ONNX graph |
| 2 | `inference.py` | `encoder.onnx` → 16-d embedding, int8 copy for the mesh |
| 3 | `mesh.py`, `node.py` | own packet handed to the MCU (`$EMB`) for its TDMA slot; neighbours' `$NBR` packets decoded and kept per round; if any neighbour reported within this round's window, `graph.onnx` runs over the ego-network histories (K=2), else the standalone result stands — **never blocks** |
| 4–5 | (inside the graphs) | nine heads and per-head temperature: `probs` is already calibrated |
| — | `alerts.py` | two-tier advisory/warning per head against the manifest's PR-curve thresholds (with their provenance), hysteresis, CAP 1.2 message, `$ALERT` siren command to the MCU, queued in SQLite |
| — | `duty_cycle.py` | hourly baseline → 5-minutely while any head is at warning (§6.4), `$CADENCE` to the MCU so the RS-485/LoRa sweep rate actually changes |
| — | `uplink.py` | alert queue flushed when the link is up, status/telemetry with the weight hash (§6.7), satellite context vector pulled (§6.3) |

Everything the cycle needs to survive a reboot is in the SQLite ring
buffer (WAL): readings, context, embeddings (own and neighbours'), the
alert queue, alert levels and duty mode.  The fire-weather cascade is
recomputed from stored history, so it needs no separate state.

## Model registry

```
<models_root>/CURRENT                 ← text: version directory to run
<models_root>/<version>/manifest.json, encoder.onnx, graph.onnx, *.int8.onnx
```
`model_registry.load_bundle` verifies every file's sha256 and the weight
hash before loading; OTA promotion/rollback is `set_current(root, version)`.

## Configuration

TOML, see `config/site.example.toml` (production template — the uplink
endpoint must be set to the real ingest service) and
`config/dev-harness.toml` (**development only**: uplink → Code C, the
test-harness mimic; RPC → the harness's TCP bridge).  Environment
overrides: `PRAHARI_UPLINK_ENDPOINT`, `PRAHARI_MODELS_ROOT`,
`PRAHARI_MODEL_VERSION`, `PRAHARI_DB_PATH`, `PRAHARI_RPC_DEVICE`,
`PRAHARI_NODE_ID`.

## Running

```sh
python3 -m prahari_hn --config /etc/prahari/site.toml          # service
python3 -m prahari_hn --config config/dev-harness.toml --once  # one cycle on stored history
../../.venv-ml/bin/python -m pytest tests -q                   # tests (no board needed)
```

The MCU<->Linux line protocol is the stand-in defined in
`../mcu/include/hn/rpc.h`; `rpc.py` is its Linux end.  Swap the transport
class when the Arduino bridge library is wired in; keep the verbs.
