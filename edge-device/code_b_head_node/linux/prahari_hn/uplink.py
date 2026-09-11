"""Uplink to L5 (§6.2 GSM/NB-IoT; §8.3 ingest).  The endpoint is
configuration: Code C (test-harness mimic) in development, the real cloud
in production — see config/.  Nothing in the decision path crosses this
link (§8.1); when it is down alerts and status queue in SQLite and are
flushed on restoration, and the satellite context vector (§6.3) simply
stops updating (its channels age out of the window and get masked).

Transport is JSON over HTTP(S) with the standard library:
    POST {endpoint}/alerts            one CAP message (dict) per request
    POST {endpoint}/status            node status/telemetry (weight hash, levels, operational params)
    GET  {endpoint}/context/{node_id} → {"ts": epoch_s, "values": {"P47": .., ...}} or 204
An MQTT transport can be dropped in behind the same three calls when the
ingest side speaks it; the pluggable class is the point, not HTTP.
"""
from __future__ import annotations

import json
import time
import urllib.error
import urllib.request

from .config import UplinkConfig
from .store import Store


class Transport:
    def post(self, path: str, payload: dict) -> bool: ...
    def get(self, path: str) -> dict | None: ...


class NullTransport(Transport):
    """No endpoint configured: always 'down'.  The node runs standalone."""

    def post(self, path, payload):
        return False

    def get(self, path):
        return None


class HttpJsonTransport(Transport):
    def __init__(self, endpoint: str, timeout_s: float = 10.0):
        self.base, self.timeout = endpoint.rstrip("/"), timeout_s

    def post(self, path, payload):
        req = urllib.request.Request(f"{self.base}/{path.lstrip('/')}", data=json.dumps(payload).encode(),
                                     headers={"Content-Type": "application/json"}, method="POST")
        try:
            with urllib.request.urlopen(req, timeout=self.timeout) as r:
                return 200 <= r.status < 300
        except (urllib.error.URLError, OSError, ValueError):
            return False

    def get(self, path):
        try:
            with urllib.request.urlopen(f"{self.base}/{path.lstrip('/')}", timeout=self.timeout) as r:
                if r.status == 204:
                    return None
                return json.loads(r.read().decode())
        except (urllib.error.URLError, OSError, ValueError):
            return None


def make_transport(cfg: UplinkConfig) -> Transport:
    if cfg.transport == "none" or not cfg.endpoint:
        return NullTransport()
    if cfg.transport == "http":
        return HttpJsonTransport(cfg.endpoint, cfg.timeout_s)
    raise ValueError(f"unknown uplink transport {cfg.transport!r}")


class Uplink:
    def __init__(self, cfg: UplinkConfig, node_id: str, store: Store, transport: Transport | None = None):
        self.cfg, self.node_id, self.store = cfg, node_id, store
        self.transport = transport or make_transport(cfg)
        self.up: bool | None = None          # last known link state
        self.sent_alerts = 0
        self.failed_attempts = 0

    def flush_alerts(self, now_ts: int | None = None) -> int:
        """Send queued CAP messages oldest-first; stop at the first failure."""
        now = int(now_ts if now_ts is not None else time.time())
        n = 0
        for aid, cap in self.store.unsent_alerts():
            if self.transport.post("alerts", {"node_id": self.node_id, "cap": cap}):
                self.store.mark_sent(aid, now)
                n += 1
                self.up = True
            else:
                self.up = False
                self.failed_attempts += 1
                break
        self.sent_alerts += n
        return n

    def send_status(self, status: dict) -> bool:
        ok = self.transport.post("status", {"node_id": self.node_id, **status})
        self.up = ok
        if not ok:
            self.failed_attempts += 1
        return ok

    def poll_context(self) -> dict | None:
        """Fetch the latest satellite/reanalysis context vector and store it."""
        if not self.cfg.context_poll:
            return None
        ctx = self.transport.get(f"context/{self.node_id}")
        if not ctx or "values" not in ctx:
            return None
        self.up = True
        values = {k: float(v) for k, v in ctx["values"].items() if k.startswith("P") and 47 <= int(k[1:]) <= 69}
        self.store.put_context(int(ctx.get("ts", time.time())), values)
        return values
