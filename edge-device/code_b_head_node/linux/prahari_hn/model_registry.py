"""Model registry convention and artifact loading.

    <models_root>/
        CURRENT                  text file: the version directory to run
        <version>/manifest.json  produced by training_pipeline/ (export.py)
        <version>/encoder.onnx, graph.onnx, encoder.int8.onnx, graph.int8.onnx

A model is selected by version name, or by ``"current"`` which reads the
CURRENT file.  Every file's sha256 is checked against the manifest and the
weight hash (sha256 over encoder.onnx + graph.onnx, the value each node
reports per §6.7) is recomputed before anything is loaded — a corrupted or
half-copied OTA push is refused, never run.  Staged OTA with rollback (§8.5)
is: copy the new version directory, verify with ``verify_version``, then
rewrite CURRENT; rollback is rewriting CURRENT back.
"""
from __future__ import annotations

import hashlib
import json
from dataclasses import dataclass
from pathlib import Path


class ModelRegistryError(RuntimeError):
    pass


@dataclass(frozen=True)
class Threshold:
    output: str
    advisory: float
    warning: float
    source: str


@dataclass
class ModelBundle:
    version: str
    directory: Path
    weight_hash: str
    channels: list[str]                 # 41, model input order
    norm_mean: list[float]
    norm_std: list[float]
    outputs: list[str]                  # 15 output names
    heads: list[dict]                   # code, name, horizons_h, graph, trained
    temperatures: dict[str, float]      # per head code (already applied inside the ONNX graphs)
    thresholds: dict[str, Threshold]    # per output
    context_length: int
    n_channels: int
    emb_dim: int
    emb_int8_scale: float
    graph_periods: int
    max_neighbours: int
    encoder_path: Path
    graph_path: Path
    training_data_note: str
    manifest: dict

    def head_of(self, output: str) -> dict:
        code = output.split("_")[0]
        return next(h for h in self.heads if h["code"] == code)

    def trained_outputs(self) -> list[str]:
        return [o for o in self.outputs if self.head_of(o)["trained"]]


def _sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def resolve_version(models_root: str | Path, version: str = "current") -> Path:
    root = Path(models_root)
    if version == "current":
        cur = root / "CURRENT"
        if not cur.exists():
            raise ModelRegistryError(f"{cur} missing: no current model selected")
        version = cur.read_text().strip()
    d = root / version
    if not (d / "manifest.json").exists():
        raise ModelRegistryError(f"model version {version!r} has no manifest under {root}")
    return d


def verify_version(directory: Path) -> dict:
    """Check every artifact file's sha256 and the weight hash.  Raises on mismatch."""
    m = json.loads((directory / "manifest.json").read_text())
    for name, expected in m["file_sha256"].items():
        p = directory / name
        if not p.exists():
            raise ModelRegistryError(f"{p} missing")
        got = _sha256(p)
        if got != expected:
            raise ModelRegistryError(f"{p}: sha256 {got} != manifest {expected}")
    h = hashlib.sha256()
    for key in ("encoder_fp32", "graph_fp32"):
        h.update((directory / m["files"][key]).read_bytes())
    if h.hexdigest() != m["weight_hash_sha256"]:
        raise ModelRegistryError("weight hash mismatch")
    return m


def load_bundle(models_root: str | Path, version: str = "current", prefer_int8: bool = True) -> ModelBundle:
    d = resolve_version(models_root, version)
    m = verify_version(d)
    files = m["files"]
    use_int8 = prefer_int8 and "encoder_int8" in files and "graph_int8" in files
    enc = d / files["encoder_int8" if use_int8 else "encoder_fp32"]
    gr = d / files["graph_int8" if use_int8 else "graph_fp32"]
    thr = {t["output"]: Threshold(t["output"], float(t["advisory"]), float(t["warning"]), str(t["source"]))
           for t in m["thresholds"]}
    enc_meta, g_meta = m["encoder"], m["graph_stage"]
    if enc_meta["n_channels"] != len(m["channels"]):
        raise ModelRegistryError("manifest channel count inconsistent")
    return ModelBundle(
        version=d.name, directory=d, weight_hash=m["weight_hash_sha256"], channels=list(m["channels"]),
        norm_mean=list(m["normalisation"]["mean"]), norm_std=list(m["normalisation"]["std"]),
        outputs=list(m["outputs"]), heads=list(m["heads"]), temperatures=dict(m["temperatures"]), thresholds=thr,
        context_length=int(enc_meta["context_length"]), n_channels=int(enc_meta["n_channels"]),
        emb_dim=int(enc_meta["embedding_dim"]), emb_int8_scale=float(enc_meta["emb_int8_scale"]),
        graph_periods=int(g_meta["periods"]), max_neighbours=int(g_meta["max_neighbours"]),
        encoder_path=enc, graph_path=gr, training_data_note=str(m.get("TRAINING_DATA", "")), manifest=m)


def set_current(models_root: str | Path, version: str) -> None:
    """Activate a verified version (OTA promotion / rollback)."""
    d = Path(models_root) / version
    verify_version(d)
    tmp = Path(models_root) / "CURRENT.tmp"
    tmp.write_text(version + "\n")
    tmp.replace(Path(models_root) / "CURRENT")
