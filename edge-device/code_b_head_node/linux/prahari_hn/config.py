"""Node configuration (TOML) with environment overrides.

The uplink endpoint is *configuration*, never code: in development it is
pointed at Code C, the test-harness cloud/gateway mimic (config/dev-harness.toml,
loudly labelled); in production it is the real ingest endpoint
(config/site.example.toml).  An unset endpoint is valid — the node then runs
fully standalone and queues everything it would have sent (§2: "the uplink →
alerts queue locally, forecasting continues").
"""
from __future__ import annotations

import os
import tomllib
from dataclasses import dataclass, field
from pathlib import Path

ENV_PREFIX = "PRAHARI_"


@dataclass
class UplinkConfig:
    endpoint: str | None = None          # e.g. "https://ingest.example.in/v1" — REQUIRED for production
    transport: str = "http"              # "http" (JSON over HTTP(S)) | "none"
    timeout_s: float = 10.0
    status_every_cycles: int = 1
    context_poll: bool = True            # fetch the satellite context vector (§6.3) when the link is up


@dataclass
class PodConfig:
    addr: int
    position: str                        # S1 S2 S3 G U C (Mode W) or R1.. (Mode R, informational)


@dataclass
class NeighbourConfig:
    node_id: str
    x_m: float | None = None
    y_m: float | None = None


@dataclass
class Config:
    node_id: str = "prahari-node"
    site_name: str = ""
    rpc_device: str = "loopback"          # "/dev/ttyHS1", "tcp://host:port", or "loopback" (tests)
    rpc_baud: int = 115200
    db_path: str = "/var/lib/prahari/ring.sqlite"
    models_root: str = "/var/lib/prahari/models"
    model_version: str = "current"        # a version directory name, or "current" → models_root/CURRENT
    prefer_int8: bool = True              # §6.7: INT8 quantised graphs when present
    ort_threads: int = 4                  # §6.7: 4 threads
    baseline_cadence_s: int = 3600        # §6.4: hourly at baseline
    elevated_cadence_s: int = 300         # §6.4: 5-minutely once any head crosses warning
    elevated_cooldown_cycles: int = 12    # cycles below warning before dropping back to baseline
    round_window_s: int = 900             # a neighbour packet older than this is "late" for the round
    siren_warning_s: int = 600
    siren_advisory_s: int = 120
    alert_clear_cycles: int = 3           # consecutive below-threshold cycles before downgrading
    retention_days: int = 60              # 21 d context window + 30 d antecedent history + 3 d cascade spin-up (§6.7)
    static: dict[str, float] = field(default_factory=dict)   # P70–P82 (P77 as P77_lat/P77_lon), S30–S34
    pods: list[PodConfig] = field(default_factory=list)
    neighbours: list[NeighbourConfig] = field(default_factory=list)
    uplink: UplinkConfig = field(default_factory=UplinkConfig)

    @property
    def pod_addrs(self) -> list[int]:
        return [p.addr for p in self.pods]


def load_config(path: str | Path) -> Config:
    raw = tomllib.loads(Path(path).read_text())
    cfg = Config()
    for k, v in raw.get("node", {}).items():
        if hasattr(cfg, k) and k not in ("static", "pods", "neighbours", "uplink"):
            setattr(cfg, k, v)
    cfg.static = {k: float(v) for k, v in raw.get("static", {}).items()}
    cfg.pods = [PodConfig(int(p["addr"]), str(p["position"])) for p in raw.get("pods", [])]
    cfg.neighbours = [NeighbourConfig(n["node_id"], n.get("x_m"), n.get("y_m")) for n in raw.get("neighbours", [])]
    up = raw.get("uplink", {})
    cfg.uplink = UplinkConfig(endpoint=up.get("endpoint") or None, transport=up.get("transport", "http"),
                              timeout_s=float(up.get("timeout_s", 10.0)),
                              status_every_cycles=int(up.get("status_every_cycles", 1)),
                              context_poll=bool(up.get("context_poll", True)))
    apply_env_overrides(cfg)
    return cfg


def apply_env_overrides(cfg: Config, env: dict[str, str] | None = None) -> Config:
    env = os.environ if env is None else env
    if (v := env.get(ENV_PREFIX + "UPLINK_ENDPOINT")) is not None:
        cfg.uplink.endpoint = v or None
    if (v := env.get(ENV_PREFIX + "MODELS_ROOT")):
        cfg.models_root = v
    if (v := env.get(ENV_PREFIX + "MODEL_VERSION")):
        cfg.model_version = v
    if (v := env.get(ENV_PREFIX + "DB_PATH")):
        cfg.db_path = v
    if (v := env.get(ENV_PREFIX + "RPC_DEVICE")):
        cfg.rpc_device = v
    if (v := env.get(ENV_PREFIX + "NODE_ID")):
        cfg.node_id = v
    return cfg
