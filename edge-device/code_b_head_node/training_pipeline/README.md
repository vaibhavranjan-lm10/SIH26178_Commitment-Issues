# Code B — offline training / export pipeline

**This directory never runs on the UNO Q.**  It is a build tool for Code B:
it runs on a developer machine, trains the head-node model, and emits the
versioned ONNX artifact that `code_b_head_node/linux/` loads.  Nothing in
here is on-device code, nothing in here is imported by the head-node
runtime, and no inference for any real site ever happens here (blueprint
§8: "the cloud learns, the nodes decide").

## Status — synthetic data only

The five open data-pipeline decisions (CLAUDE.md) are unresolved, so there
is no real training sample.  `prahari_train/synthetic.py` is a **placeholder
generator** — not an ERA5 / IMERG / SMAP / GloFAS / MODIS / Sentinel
pipeline — and every artifact this pipeline currently produces is trained
on it.  Metrics in `training_report.json` describe fit to that made-up
corpus, not real-world skill.  Assumptions made in lieu of the decisions:

| Open decision | What the pipeline does now |
|---|---|
| 1 index provenance | NDVI/NDMI/NDWI computed from bands (S25–S27), per the parameter doc |
| 2 resampling rule | assumes a uniform **hourly** grid; `features.cadence_hours` refuses anything else |
| 3 missing data | **mask, never impute** — enforced in `features.py`, `data.py`, the model input |
| 4 normalisation scope | **global** z-score over pooled training columns, stored in the artifact |
| 5 soil depth count | three depths (P1–P3); layer thicknesses in `features.LAYER_THICKNESS_M` |

## What is in here

```
prahari_train/
  registry.py    the 146-parameter taxonomy (82/39/17/8; 41 trained = 16/11/12/2), lookups, count checks
  features.py    S1–S39 and T1–T17 per the parameter doc's "From" column (proxies documented per function)
  heads.py       the nine hazard heads — horizons, graph-stage membership (FL UF FI PO), trained/declared
  synthetic.py   PLACEHOLDER multi-column corpus with propagating events and labels
  data.py        features → 41 channels + mask, held-out-COLUMN split, windows, ego-networks, embedding cache
  model.py       Granite TTM encoder → A3TGCN graph stage → 9 heads → per-head temperature (+ export wrappers)
  calibrate.py   temperature fitting; advisory/warning thresholds from the validation PR curve, labelled fallbacks
  export.py      ONNX export (dynamo exporter pinned, opset 18), onnxruntime verification, INT8 copies, manifest
  train.py       the end-to-end script
tests/           one file per module + an end-to-end smoke run
artifacts/       output of train.py (versioned directories; not committed)
```

Dependencies are the genuine packages: `granite-tsfm` (pretrained
`ibm-granite/granite-timeseries-ttm-r2`, fetched from the HF hub on first
use) and `torch_geometric_temporal` (`A3TGCN`).  The one re-expression is
`model.DenseGraphStage`, an export-only dense-adjacency rewrite of A3TGCN
with copied weights, asserted equal to the original in tests — it exists
because the on-device runtime wants a fixed-shape graph input.

## Model (blueprint §6.7)

| Stage | Here | Params |
|---|---|---|
| 1 normalise + mask | `PrahariModel.normalise`; 41 values × mask + 41 mask channels | — |
| 2 encoder → 16-d | Granite TTM r2 backbone (context 512 h) + linear projection | ~0.77 M |
| 3 graph (optional) | A3TGCN over the K=2 ego-network of received embeddings, last 3 rounds, residual; **only FL, UF, FI, PO** | ~2.7 k |
| 4 nine heads | one MLP per head, 15 logits (`heads.OUTPUT_NAMES`); GL, WQ declared-only (no loss) | ~5.4 k each |
| 5 temperature | nine scalars fitted on validation logits | 9 |

Training is two-phase: the standalone path (1, 2, 4, 5) first — that path
*is* the "node cut off from the mesh still forecasts" claim — then the
encoder is frozen, every cycle's embedding is cached (as the node would),
and the graph stage plus the four graph heads are trained on ego-network
samples with random neighbour drop-out and a share of graph-off samples so
those heads keep working when stage 3 is skipped.

Validation is by **held-out column**, not a temporal cut (see
`data.split_by_column` for why).  Thresholds are never preset: each output
gets advisory/warning levels from the validation precision-recall curve,
and any output with too few validation positives gets an explicitly
labelled fallback (`source` field in the manifest).

## Artifact layout

```
artifacts/<version>/
  encoder.onnx        stages 1,2,4,5 — x(1,512,41), mask(1,512,41) → emb, emb_int8, logits, probs
  graph.onnx          stage 3+4+5 — own_emb(1,16), hist(1,9,16,3), adj(1,9,9), valid(1,9) → refined, logits, probs
  encoder.int8.onnx   dynamically quantised copies (§6.7 INT8); deviation vs fp32 in manifest.verification
  graph.int8.onnx
  manifest.json       weight hash (what each node reports, §6.7), channel order, normalisation,
                      temperatures, thresholds + sources, verification numbers, full training report
  model.pt            torch state dict (for re-export / inspection only)
  training_report.json, training_log.txt
```

The runtime always runs `encoder.onnx`; it runs `graph.onnx` only when
neighbour embeddings arrived this round.  Batch size is fixed at 1.

## Running

```sh
cd code_b_head_node/training_pipeline
../../.venv-ml/bin/python -m pytest tests -q                 # unit + end-to-end smoke (≈1 min)
../../.venv-ml/bin/python -m prahari_train.train --out artifacts/v0.1.0-synthetic   # full synthetic run
```

See `requirements.txt` for the pinned environment (CPU torch 2.11 — the
`granite-tsfm` pin — plus prebuilt `torch-sparse`/`torch-scatter` wheels).
