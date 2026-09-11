"""End-to-end smoke: tiny corpus, random-init TTM, 1 epoch each, ONNX verified.
Also unit-tests the calibration fallbacks and the split rule."""
import json

import numpy as np
import onnxruntime as ort
import pytest

from prahari_train import calibrate as C
from prahari_train import data as D
from prahari_train import heads as H
from prahari_train import synthetic as S
from prahari_train import train as T


def test_split_is_by_column_and_stratified():
    corpus = S.generate_corpus(n_columns=8, n_days=30, seed=2)
    cols = D.prepare_columns(corpus)
    tr, va = D.split_by_column(cols, 0.25, seed=0)
    assert sorted(tr + va) == list(range(8)) and len(va) == 2 and not set(tr) & set(va)
    assert len({cols[i].profile for i in va}) == 2
    assert "temporal" in D.split_by_column.__doc__ and "zero positives" in D.split_by_column.__doc__


def test_pr_thresholds_and_fallbacks():
    rng = np.random.default_rng(0)
    y = (rng.random(2000) < 0.1).astype(np.float32)
    p = np.clip(0.6 * y + 0.2 * rng.random(2000), 0, 1)
    a, w, ap = C.pr_thresholds(p, y)
    assert 0 < a <= w < 1 and ap > 0.8
    # recall at the advisory threshold is ≥ 0.9
    assert ((p >= a) & (y == 1)).sum() / y.sum() >= 0.9
    # assemble a 15-wide problem: CY has 2 val positives, GL/WQ untrained
    val_y = np.zeros((500, 15), np.float32); tr_y = np.zeros((3000, 15), np.float32)
    val_p = rng.random((500, 15)).astype(np.float32); tr_p = rng.random((3000, 15)).astype(np.float32)
    k_fl, k_cy, k_hw = H.OUTPUT_NAMES.index("FL_t0"), H.OUTPUT_NAMES.index("CY_t48"), H.OUTPUT_NAMES.index("HW_t72")
    val_y[:60, k_fl] = 1; val_p[:60, k_fl] += 1; tr_y[:300, k_fl] = 1
    val_y[:2, k_cy] = 1; tr_y[:40, k_cy] = 1; tr_p[:40, k_cy] += 1          # too few val → train fallback
    # HW: nothing anywhere → fixed fallback
    res = {t.output: t for t in C.calibrate_thresholds(val_p, val_y, tr_p, tr_y)}
    assert res["FL_t0"].source == C.SOURCE_VAL and res["FL_t0"].n_val_pos == 60
    assert res["CY_t48"].source == C.SOURCE_TRAIN and "FALLBACK" in res["CY_t48"].source
    assert res["HW_t72"].source == C.SOURCE_FIXED and (res["HW_t72"].advisory, res["HW_t72"].warning) == C.FIXED_FALLBACK
    assert res["GL_t0"].source == C.SOURCE_DECLARED and res["WQ_t24"].source == C.SOURCE_DECLARED


def test_temperature_fit_reduces_nll():
    rng = np.random.default_rng(1)
    z = rng.normal(-1, 2, (6000, 15))
    y = (rng.random(z.shape) < 1 / (1 + np.exp(-z))).astype(np.float32)   # calibrated at logit z
    logits = (3.0 * z).astype(np.float32)                                    # over-confident ×3 → T ≈ 3
    T = C.fit_temperatures(logits, y)
    assert T.shape == (9,) and (np.abs(T[:7] - 3.0) < 0.5).all() and (T[7:] == 1).all()


@pytest.mark.slow
def test_end_to_end_tiny_pipeline(tmp_path):
    args = T.parse_args(["--out", str(tmp_path / "art"), "--columns", "4", "--days", "45", "--epochs", "1",
                         "--graph-epochs", "1", "--stride", "24", "--val-stride", "24", "--batch", "8",
                         "--no-pretrained", "--threads", "4", "--max-train-batches", "3", "--version", "test"])
    manifest = T.run(args)
    out = tmp_path / "art"
    for f in ("encoder.onnx", "graph.onnx", "encoder.int8.onnx", "graph.int8.onnx", "manifest.json", "training_report.json", "model.pt"):
        assert (out / f).exists(), f
    m = json.loads((out / "manifest.json").read_text())
    assert m["channels"] == manifest["channels"] and len(m["channels"]) == 41
    assert "SYNTHETIC" in m["TRAINING_DATA"]
    assert len(m["thresholds"]) == 15 and all("source" in t for t in m["thresholds"])
    assert m["exporter"]["mode"].startswith("dynamo") and m["exporter"]["opset"] == 18
    assert len(m["training_report"]["phase1_standalone"]) == 1
    rec = m["training_report"]["phase1_standalone"][0]
    assert "train_loss" in rec and "val_loss" in rec
    # the exported file runs at deployment shape
    s = ort.InferenceSession(str(out / "encoder.onnx"), providers=["CPUExecutionProvider"])
    o = s.run(None, {"x": np.zeros((1, 512, 41), np.float32), "mask": np.ones((1, 512, 41), np.float32)})
    assert o[3].shape == (1, 15) and o[1].dtype == np.int8
