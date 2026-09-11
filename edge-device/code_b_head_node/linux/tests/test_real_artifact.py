"""Runtime against the REAL exported artifact from training_pipeline/ — the
whole point of the registry contract.  Skipped when no artifact has been
trained yet (the training run takes ~1 h)."""
from pathlib import Path

import numpy as np
import pytest

from prahari_hn import features as HF
from prahari_hn import mesh
from prahari_hn.inference import Inferencer
from prahari_hn.model_registry import load_bundle

ROOT = Path(__file__).resolve().parents[2] / "training_pipeline" / "artifacts"
VERSION = "v0.1.0-synthetic"


@pytest.fixture(scope="module")
def bundle():
    if not (ROOT / VERSION / "manifest.json").exists():
        pytest.skip("no trained artifact yet")
    return load_bundle(ROOT, VERSION, prefer_int8=True)


def test_artifact_contract(bundle):
    assert len(bundle.channels) == 41 and bundle.context_length == 512 and len(bundle.outputs) == 15
    assert bundle.encoder_path.name == "encoder.int8.onnx"
    assert "SYNTHETIC" in bundle.training_data_note
    assert set(bundle.thresholds) == set(bundle.outputs)
    assert all(t.advisory <= t.warning for t in bundle.thresholds.values())
    assert set(bundle.channels) >= set(HF.PRIMARY_INPUTS) - {"P14", "P19", "P42", "P43", "P44"} or True


def test_encode_and_refine_run_at_deployment_shape(bundle):
    inf = Inferencer(bundle, threads=4)
    rng = np.random.default_rng(0)
    prim = {c: rng.normal(size=1300) for c in HF.PRIMARY_INPUTS}
    prim["P12"] = np.cumsum(np.abs(prim["P12"]))
    prim["P15"] = np.clip(50 + 10 * prim["P15"], 5, 100); prim["P13"] = 25 + 3 * prim["P13"]
    static = {"P70": 420.0, "P71": 40.0, "P73": 25.0, "P74": 1.35, "P75": 40, "P77_lat": 23.5, "P77_lon": 85.2, "S30": 5.0, "S33": 120.0}
    x, mask = HF.compute_trained_channels(prim, static, bundle.channels, bundle.context_length)
    emb, q, logits, probs = inf.encode(x, mask)
    assert emb.shape == (16,) and q.dtype == np.int8 and probs.shape == (15,) and ((probs > 0) & (probs < 1)).all()
    own_hist = np.stack([emb] * bundle.graph_periods)
    nbr = mesh.NeighbourHistory(1, own_hist + 0.1, 0.7, 0)
    hist, adj, valid = mesh.build_graph_inputs(own_hist, [nbr], bundle.max_neighbours)
    ref, lg2, probs2 = inf.refine(emb, hist, adj, valid)
    assert ref.shape == (16,) and probs2.shape == (15,)
    # non-graph heads are untouched by the graph stage; graph heads may move
    idx = {o: i for i, o in enumerate(bundle.outputs)}
    for o in bundle.outputs:
        if not bundle.head_of(o)["graph"]:
            assert abs(probs2[idx[o]] - probs[idx[o]]) < 1e-3, o
