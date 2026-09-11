"""onnxruntime wrapper for the two artifact graphs (§6.7: CPU EP, 4 threads,
INT8 when available).  Stage 1's normalisation is inside encoder.onnx, so
this side feeds *raw* physical values plus the mask; stage 5's temperature
scaling is inside both graphs, so ``probs`` is already calibrated."""
from __future__ import annotations

import numpy as np

from .model_registry import ModelBundle


class Inferencer:
    def __init__(self, bundle: ModelBundle, threads: int = 4):
        import onnxruntime as ort
        so = ort.SessionOptions()
        so.intra_op_num_threads = threads
        so.inter_op_num_threads = 1
        self.bundle = bundle
        self.enc = ort.InferenceSession(str(bundle.encoder_path), so, providers=["CPUExecutionProvider"])
        self.gr = ort.InferenceSession(str(bundle.graph_path), so, providers=["CPUExecutionProvider"])
        enc_in = {i.name: i.shape for i in self.enc.get_inputs()}
        assert set(enc_in) == {"x", "mask"}, enc_in
        assert list(enc_in["x"]) == [1, bundle.context_length, bundle.n_channels], enc_in
        assert {i.name for i in self.gr.get_inputs()} == {"own_emb", "hist", "adj", "valid"}

    def encode(self, x: np.ndarray, mask: np.ndarray):
        """x, mask (L, C) → emb (16,), emb_int8 (16,), logits (15,), probs (15,)"""
        emb, q, lg, pr = self.enc.run(None, {"x": x[None].astype(np.float32), "mask": mask[None].astype(np.float32)})
        return emb[0], q[0], lg[0], pr[0]

    def refine(self, own_emb: np.ndarray, hist: np.ndarray, adj: np.ndarray, valid: np.ndarray):
        """Stage 3 + heads on the refined embedding.  hist (1,N,16,P), adj (1,N,N), valid (1,N)."""
        ref, lg, pr = self.gr.run(None, {"own_emb": own_emb[None].astype(np.float32), "hist": hist.astype(np.float32),
                                          "adj": adj.astype(np.float32), "valid": valid.astype(np.float32)})
        return ref[0], lg[0], pr[0]
