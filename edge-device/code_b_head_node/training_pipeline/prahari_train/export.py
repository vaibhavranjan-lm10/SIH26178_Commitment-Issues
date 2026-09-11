"""ONNX export of the trained model, verified in onnxruntime before it is
called done (CLAUDE.md pitfall: "pin the exporter mode explicitly, then
actually load the exported file … and run a forward pass").

Two graphs make up one versioned artifact (blueprint §6.7 "stage 3 is
skippable"):

  encoder.onnx   stages 1,2,4,5 — x (B,512,41), mask (B,512,41)
                 → emb (B,16), emb_int8 (B,16), logits (B,15), probs (B,15)
  graph.onnx     stage 3 + 4,5 — own_emb (B,16), hist (B,9,16,P), adj (B,9,9),
                 valid (B,9) → refined (B,16), logits (B,15), probs (B,15)

plus INT8 dynamically-quantised copies (§6.7: INT8) and manifest.json with
the weight hash every node reports (§6.7 model versioning), the channel
order, normalisation, temperatures, thresholds and the training report.

Exporter: torch.onnx.export(..., dynamo=True, opset_version=18) — the
torch.export/dynamo exporter, pinned explicitly (EXPORTER_MODE).  The
legacy TorchScript exporter (dynamo=False) was tried first and REJECTED:
with torch 2.11 it traces TTM's patch-mixer into a graph that fails ONNX
shape inference ("Incompatible dimensions for matrix multiplication" at
.../patch_mixer/mlp/fc1/MatMul) — the file is written but cannot even be
loaded.  That is exactly the failure the "load it and run it" rule exists
to catch.  Batch dimension is FIXED at 1: the head node runs one window per
inference cycle; the verification below runs each sample separately.
"""
from __future__ import annotations

import hashlib
import json
import time
from pathlib import Path

import numpy as np
import onnxruntime as ort
import torch

from . import heads as H
from . import model as M
from . import registry

OPSET = 18
EXPORTER_MODE = "dynamo (torch.export), opset 18"
ORT_THREADS = 4     # blueprint §6.7


def _session(path: Path) -> ort.InferenceSession:
    so = ort.SessionOptions()
    so.intra_op_num_threads = ORT_THREADS
    so.inter_op_num_threads = 1
    return ort.InferenceSession(str(path), so, providers=["CPUExecutionProvider"])


def _sha256(*paths: Path) -> str:
    h = hashlib.sha256()
    for p in paths:
        h.update(p.read_bytes())
    return h.hexdigest()


def _strip_stale_value_info(path: Path) -> None:
    """The dynamo exporter's post-optimiser can leave intermediate value_info
    entries whose shapes no longer match the graph (seen: a transposed Linear
    weight annotated (15744) vs (16)).  onnxruntime ignores them, but the
    quantiser's strict shape inference does not.  Drop them and re-check."""
    import onnx
    m = onnx.load(str(path))
    del m.graph.value_info[:]
    onnx.checker.check_model(m)
    m = onnx.shape_inference.infer_shapes(m, strict_mode=True)
    onnx.save(m, str(path))


def _quantise(fp32: Path, int8: Path) -> None:
    from onnxruntime.quantization import QuantType, quantize_dynamic
    quantize_dynamic(str(fp32), str(int8), weight_type=QuantType.QInt8)


def export_and_verify(model: M.PrahariModel, out_dir: Path, sample_x: torch.Tensor, sample_mask: torch.Tensor,
                      sample_graph: tuple[torch.Tensor, torch.Tensor, torch.Tensor, torch.Tensor],
                      thresholds: list[dict], report: dict, version: str) -> dict:
    out_dir.mkdir(parents=True, exist_ok=True)
    model.eval()
    enc, gr = M.EncoderExport(model).eval(), M.GraphExport(model).eval()
    enc_path, gr_path = out_dir / "encoder.onnx", out_dir / "graph.onnx"

    own, hist, adj, valid = sample_graph
    with torch.no_grad():
        torch.onnx.export(enc, (sample_x[:1], sample_mask[:1]), str(enc_path), dynamo=True, opset_version=OPSET,
                          input_names=["x", "mask"], output_names=["emb", "emb_int8", "logits", "probs"])
        torch.onnx.export(gr, (own[:1], hist[:1], adj[:1], valid[:1]), str(gr_path), dynamo=True, opset_version=OPSET,
                          input_names=["own_emb", "hist", "adj", "valid"], output_names=["refined", "logits", "probs"])

    _strip_stale_value_info(enc_path)
    _strip_stale_value_info(gr_path)

    def run_all(sess, feeds: dict[str, np.ndarray]) -> list[np.ndarray]:
        """Run the batch-1 graph once per sample and stack — the deployment shape."""
        n = next(iter(feeds.values())).shape[0]
        outs = [sess.run(None, {k: v[i: i + 1] for k, v in feeds.items()}) for i in range(n)]
        return [np.concatenate([o[j] for o in outs], 0) for j in range(len(outs[0]))]

    # ---- verify fp32 against torch, sample by sample
    verification = {}
    with torch.no_grad():
        ref = enc(sample_x, sample_mask)
        ref_g = gr(own, hist, adj, valid)
    enc_feeds = {"x": sample_x.numpy(), "mask": sample_mask.numpy()}
    gr_feeds = {"own_emb": own.numpy(), "hist": hist.numpy(), "adj": adj.numpy(), "valid": valid.numpy()}
    s = _session(enc_path)
    o = run_all(s, enc_feeds)
    def rel(a, b):
        """max |a−b| / (max |b| + 1): relative for large tensors, absolute for small."""
        return float(np.abs(a - b).max() / (np.abs(b).max() + 1.0))

    verification["encoder_fp32_vs_torch"] = {
        "emb_max_abs": float(np.abs(o[0] - ref[0].numpy()).max()), "emb_rel": rel(o[0], ref[0].numpy()),
        "emb_scale_max_abs": float(np.abs(ref[0].numpy()).max()),
        "logits_rel": rel(o[2], ref[2].numpy()), "probs_max_abs": float(np.abs(o[3] - ref[3].numpy()).max()),
        "emb_int8_mismatches": int((o[1] != ref[1].numpy()).sum())}
    assert o[1].dtype == np.int8 and o[3].shape == (sample_x.shape[0], H.N_OUTPUTS)
    sg = _session(gr_path)
    og = run_all(sg, gr_feeds)
    verification["graph_fp32_vs_torch"] = {
        "refined_rel": rel(og[0], ref_g[0].numpy()), "logits_rel": rel(og[1], ref_g[1].numpy()),
        "probs_max_abs": float(np.abs(og[2] - ref_g[2].numpy()).max())}
    for k, v in {**verification["encoder_fp32_vs_torch"], **verification["graph_fp32_vs_torch"]}.items():
        if k.endswith("_rel") or k == "probs_max_abs":
            assert v < 1e-4, f"fp32 ONNX diverges from torch on {k}: {v}"
    # int8 mesh payload: a 1-LSB disagreement on a rounding boundary is expected fp noise, more is not
    assert verification["encoder_fp32_vs_torch"]["emb_int8_mismatches"] <= 2

    # ---- INT8 dynamic quantisation (§6.7) — verified to run; deviation reported, not hidden
    enc_q, gr_q = out_dir / "encoder.int8.onnx", out_dir / "graph.int8.onnx"
    _quantise(enc_path, enc_q)
    _quantise(gr_path, gr_q)
    sq = _session(enc_q)
    t0 = time.perf_counter(); oq = run_all(sq, enc_feeds); dt_q = (time.perf_counter() - t0) / sample_x.shape[0]
    t0 = time.perf_counter(); run_all(s, enc_feeds); dt_f = (time.perf_counter() - t0) / sample_x.shape[0]
    ogq = run_all(_session(gr_q), gr_feeds)
    verification["encoder_int8_vs_fp32_max_abs_diff"] = {"emb": float(np.abs(oq[0] - o[0]).max()), "probs": float(np.abs(oq[3] - o[3]).max())}
    verification["graph_int8_vs_fp32_max_abs_diff"] = {"refined": float(np.abs(ogq[0] - og[0]).max()), "probs": float(np.abs(ogq[2] - og[2]).max())}
    verification["encoder_latency_s_per_window_x86_host"] = {"fp32": dt_f, "int8": dt_q,
                                                            "note": "dev-machine x86 timing, not the QRB2210"}

    manifest = {
        "artifact": "prahari-head-node-model", "version": version, "created_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "TRAINING_DATA": "SYNTHETIC PLACEHOLDER CORPUS (synthetic.py) — not real observations; metrics are not real-world skill",
        "weight_hash_sha256": _sha256(enc_path, gr_path),
        "files": {"encoder_fp32": enc_path.name, "graph_fp32": gr_path.name, "encoder_int8": enc_q.name, "graph_int8": gr_q.name},
        "file_sha256": {p.name: _sha256(p) for p in (enc_path, gr_path, enc_q, gr_q)},
        "exporter": {"torch": torch.__version__, "mode": EXPORTER_MODE, "opset": OPSET,
                     "onnxruntime": ort.__version__, "ort_threads": ORT_THREADS},
        "encoder": {"pretrained_source": model.encoder.pretrained_source, "context_length": M.CONTEXT_LENGTH, "batch": 1,
                    "cadence_hours": 1.0, "n_channels": 41, "embedding_dim": M.EMB_DIM, "emb_int8_scale": M.EMB_INT8_SCALE},
        "graph_stage": {"implementation": "torch_geometric_temporal.A3TGCN (trained) → DenseGraphStage (export re-expression)",
                        "periods": M.GRAPH_PERIODS, "max_neighbours": M.MAX_NEIGHBOURS, "heads": list(H.GRAPH_HEADS)},
        "channels": registry.trained_channels(),
        "normalisation": {"scope": "global (training columns pooled; open decision 4)",
                          "mean": model.norm_mean.tolist(), "std": model.norm_std.tolist()},
        "outputs": H.OUTPUT_NAMES,
        "heads": [{"code": h.code, "name": h.name, "horizons_h": list(h.horizons_h), "graph": h.graph, "trained": h.trained} for h in H.HEADS],
        "temperatures": {h.code: float(t) for h, t in zip(H.HEADS, model.temperatures().tolist())},
        "thresholds": thresholds,
        "verification": verification,
        "training_report": report,
        "parameter_counts": model.parameter_counts(),
    }
    (out_dir / "manifest.json").write_text(json.dumps(manifest, indent=2))
    return manifest
