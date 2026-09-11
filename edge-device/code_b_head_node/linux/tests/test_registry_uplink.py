"""Model registry (hash verification, CURRENT pointer) and uplink queue semantics."""
import hashlib
import json

import pytest

from prahari_hn import model_registry as R
from prahari_hn.config import UplinkConfig, apply_env_overrides, Config, load_config
from prahari_hn.store import Store
from prahari_hn.uplink import NullTransport, Uplink, make_transport


def fake_artifact(root, version="v1"):
    d = root / version; d.mkdir(parents=True)
    files = {"encoder_fp32": "encoder.onnx", "graph_fp32": "graph.onnx", "encoder_int8": "encoder.int8.onnx", "graph_int8": "graph.int8.onnx"}
    for f in files.values():
        (d / f).write_bytes(f.encode() * 10)
    wh = hashlib.sha256((d / "encoder.onnx").read_bytes() + (d / "graph.onnx").read_bytes()).hexdigest()
    m = {"version": version, "TRAINING_DATA": "SYNTHETIC", "weight_hash_sha256": wh, "files": files,
         "file_sha256": {f: hashlib.sha256((d / f).read_bytes()).hexdigest() for f in files.values()},
         "encoder": {"context_length": 512, "n_channels": 2, "embedding_dim": 16, "emb_int8_scale": 16.0},
         "graph_stage": {"periods": 3, "max_neighbours": 8}, "channels": ["P1", "P13"],
         "normalisation": {"mean": [0, 0], "std": [1, 1]}, "outputs": ["FL_t0"],
         "heads": [{"code": "FL", "name": "f", "horizons_h": [0], "graph": True, "trained": True}],
         "temperatures": {"FL": 1.0}, "thresholds": [{"output": "FL_t0", "advisory": 0.3, "warning": 0.7, "source": "val_pr_curve"}]}
    (d / "manifest.json").write_text(json.dumps(m))
    return d


def test_registry_verifies_and_selects(tmp_path):
    fake_artifact(tmp_path, "v1")
    with pytest.raises(R.ModelRegistryError):
        R.load_bundle(tmp_path, "current")                   # no CURRENT yet
    R.set_current(tmp_path, "v1")
    b = R.load_bundle(tmp_path, "current", prefer_int8=True)
    assert b.version == "v1" and b.encoder_path.name == "encoder.int8.onnx" and b.thresholds["FL_t0"].warning == 0.7
    assert R.load_bundle(tmp_path, "v1", prefer_int8=False).encoder_path.name == "encoder.onnx"
    (tmp_path / "v1" / "graph.onnx").write_bytes(b"corrupt")   # a bad OTA copy
    with pytest.raises(R.ModelRegistryError):
        R.load_bundle(tmp_path, "v1")
    with pytest.raises(R.ModelRegistryError):
        R.set_current(tmp_path, "v1")                        # promotion refuses a broken version


def test_uplink_null_transport_queues_and_env_override(tmp_path, monkeypatch):
    s = Store(tmp_path / "r.sqlite")
    up = Uplink(UplinkConfig(endpoint=None), "n", s)
    assert isinstance(up.transport, NullTransport)
    s.queue_alert(1, "FL_t0", 2, {"identifier": "a"})
    assert up.flush_alerts(2) == 0 and up.up is False and len(s.unsent_alerts()) == 1
    assert up.send_status({"x": 1}) is False and up.poll_context() is None
    cfg = apply_env_overrides(Config(), {"PRAHARI_UPLINK_ENDPOINT": "http://cloud.example/v1", "PRAHARI_MODEL_VERSION": "v9"})
    assert cfg.uplink.endpoint == "http://cloud.example/v1" and cfg.model_version == "v9"
    assert type(make_transport(cfg.uplink)).__name__ == "HttpJsonTransport"
    with pytest.raises(ValueError):
        make_transport(UplinkConfig(endpoint="x", transport="carrier-pigeon"))


def test_config_files_parse_and_dev_is_labelled():
    import pathlib
    here = pathlib.Path(__file__).resolve().parents[1] / "config"
    prod = load_config(here / "site.example.toml")
    assert prod.uplink.endpoint is None and prod.pods[0].position == "S1" and prod.static["S30"] == 6.5
    dev = load_config(here / "dev-harness.toml")
    assert "127.0.0.1" in dev.uplink.endpoint and dev.neighbours[0].node_id == "dev-node-B"
    text = (here / "dev-harness.toml").read_text()
    assert "NOT a production configuration" in text and "Code C" in text
