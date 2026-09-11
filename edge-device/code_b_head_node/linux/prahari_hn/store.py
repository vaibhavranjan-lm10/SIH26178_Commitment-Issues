"""SQLite ring buffer — the node's memory across reboots (§6.7 "local
persistence: rolling context window plus ~30 days of history.  A reboot
mid-monsoon must not lose antecedent precipitation state").

Tables
  readings   (ts, channel, value, flags)   every usable pod channel, per sweep, physical units
  context    (ts, channel, value)          satellite/reanalysis context vector values (P47–P69)
  embeddings (ts, node_id16, own, emb)     16-byte int8 embeddings, own and neighbours', per round
  alerts     (id, created_ts, sent_ts, output, level, cap_json)   outbound queue, flushed when the uplink is up
  runs       (ts, refined, n_nbrs, weight_hash, probs_json)       inference log
  state      (key, value)                  small persisted state (duty mode, alert levels, cascade codes)

WAL journal, synchronous=NORMAL: a power cut loses at most the last
un-checkpointed transaction, never the file.  ``prune`` keeps
``retention_days`` of everything but the alert queue's unsent rows.
Nothing here ever fills a gap — a missing hour is simply absent and the
window builder masks it.
"""
from __future__ import annotations

import json
import sqlite3
import time
from pathlib import Path

import numpy as np

SCHEMA = """
CREATE TABLE IF NOT EXISTS readings (ts INTEGER NOT NULL, channel TEXT NOT NULL, value REAL NOT NULL,
    flags INTEGER NOT NULL DEFAULT 0, PRIMARY KEY (ts, channel)) WITHOUT ROWID;
CREATE INDEX IF NOT EXISTS readings_ch_ts ON readings (channel, ts);
CREATE TABLE IF NOT EXISTS context (ts INTEGER NOT NULL, channel TEXT NOT NULL, value REAL NOT NULL,
    PRIMARY KEY (ts, channel)) WITHOUT ROWID;
CREATE TABLE IF NOT EXISTS embeddings (ts INTEGER NOT NULL, node_id16 INTEGER NOT NULL, own INTEGER NOT NULL,
    emb BLOB NOT NULL, PRIMARY KEY (ts, node_id16)) WITHOUT ROWID;
CREATE TABLE IF NOT EXISTS alerts (id INTEGER PRIMARY KEY AUTOINCREMENT, created_ts INTEGER NOT NULL,
    sent_ts INTEGER, output TEXT NOT NULL, level INTEGER NOT NULL, cap_json TEXT NOT NULL);
CREATE INDEX IF NOT EXISTS alerts_unsent ON alerts (sent_ts) WHERE sent_ts IS NULL;
CREATE TABLE IF NOT EXISTS runs (ts INTEGER PRIMARY KEY, refined INTEGER NOT NULL, n_nbrs INTEGER NOT NULL,
    weight_hash TEXT NOT NULL, probs_json TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS state (key TEXT PRIMARY KEY, value TEXT NOT NULL);
"""

HOUR = 3600


class Store:
    def __init__(self, path: str | Path, retention_days: int = 30):
        self.path = str(path)
        self.retention_s = retention_days * 86400
        if self.path != ":memory:":
            Path(self.path).parent.mkdir(parents=True, exist_ok=True)
        self.db = sqlite3.connect(self.path, isolation_level=None, check_same_thread=False)
        self.db.execute("PRAGMA journal_mode=WAL")
        self.db.execute("PRAGMA synchronous=NORMAL")
        self.db.executescript(SCHEMA)
        self.integrity = self.db.execute("PRAGMA quick_check").fetchone()[0]

    def close(self) -> None:
        self.db.close()

    # ---- readings
    def put_readings(self, ts: int, values: dict[str, tuple[float, int]]) -> None:
        """values: {channel: (value, flags)}; a re-report for the same second overwrites."""
        with self.db:
            self.db.executemany("INSERT OR REPLACE INTO readings VALUES (?,?,?,?)",
                                [(int(ts), ch, float(v), int(f)) for ch, (v, f) in values.items()])

    def put_context(self, ts: int, values: dict[str, float]) -> None:
        with self.db:
            self.db.executemany("INSERT OR REPLACE INTO context VALUES (?,?,?)",
                                [(int(ts), ch, float(v)) for ch, v in values.items()])

    def hourly_matrix(self, channels: list[str], end_ts: int, hours: int) -> tuple[np.ndarray, np.ndarray]:
        """(values (hours, C), mask (hours, C)) on the hourly grid ending at the
        hour containing end_ts.  Each cell is the LAST usable reading in that
        hour (readings + context tables), NaN/0-mask if none — never filled."""
        t_end = (int(end_ts) // HOUR) * HOUR
        t0 = t_end - (hours - 1) * HOUR
        vals = np.full((hours, len(channels)), np.nan, np.float64)
        col = {c: i for i, c in enumerate(channels)}
        q = ("SELECT (ts/?)*? AS h, channel, value FROM {} WHERE ts >= ? AND ts < ? AND channel IN ({}) "
             "ORDER BY ts ASC")
        marks = ",".join("?" for _ in channels)
        for table in ("readings", "context"):
            for h, ch, v in self.db.execute(q.format(table, marks), [HOUR, HOUR, t0, t_end + HOUR, *channels]):
                vals[(int(h) - t0) // HOUR, col[ch]] = v   # ascending order → last wins
        mask = np.isfinite(vals).astype(np.float32)
        return vals, mask

    def latest_reading_ts(self) -> int | None:
        r = self.db.execute("SELECT MAX(ts) FROM readings").fetchone()[0]
        return int(r) if r is not None else None

    # ---- embeddings
    def put_embedding(self, ts: int, node_id16: int, emb_int8: np.ndarray, own: bool) -> None:
        with self.db:
            self.db.execute("INSERT OR REPLACE INTO embeddings VALUES (?,?,?,?)",
                            (int(ts), int(node_id16), 1 if own else 0, np.asarray(emb_int8, np.int8).tobytes()))

    def embedding_history(self, node_id16: int, periods: int, before_ts: int) -> list[tuple[int, np.ndarray]]:
        rows = self.db.execute("SELECT ts, emb FROM embeddings WHERE node_id16=? AND ts<=? ORDER BY ts DESC LIMIT ?",
                               (int(node_id16), int(before_ts), periods)).fetchall()
        return [(int(ts), np.frombuffer(b, np.int8).copy()) for ts, b in reversed(rows)]

    def neighbours_seen(self, since_ts: int) -> list[tuple[int, int]]:
        """[(node_id16, last_ts)] for neighbours heard since since_ts."""
        return [(int(n), int(t)) for n, t in self.db.execute(
            "SELECT node_id16, MAX(ts) FROM embeddings WHERE own=0 AND ts>=? GROUP BY node_id16", (int(since_ts),))]

    # ---- alert queue
    def queue_alert(self, created_ts: int, output: str, level: int, cap: dict) -> int:
        with self.db:
            cur = self.db.execute("INSERT INTO alerts (created_ts, sent_ts, output, level, cap_json) VALUES (?,NULL,?,?,?)",
                                  (int(created_ts), output, int(level), json.dumps(cap)))
            return int(cur.lastrowid)

    def unsent_alerts(self) -> list[tuple[int, dict]]:
        return [(int(i), json.loads(j)) for i, j in
                self.db.execute("SELECT id, cap_json FROM alerts WHERE sent_ts IS NULL ORDER BY id ASC")]

    def mark_sent(self, alert_id: int, sent_ts: int) -> None:
        with self.db:
            self.db.execute("UPDATE alerts SET sent_ts=? WHERE id=?", (int(sent_ts), int(alert_id)))

    # ---- runs / state
    def log_run(self, ts: int, refined: bool, n_nbrs: int, weight_hash: str, probs: dict[str, float]) -> None:
        with self.db:
            self.db.execute("INSERT OR REPLACE INTO runs VALUES (?,?,?,?,?)",
                            (int(ts), 1 if refined else 0, int(n_nbrs), weight_hash, json.dumps(probs)))

    def get_state(self, key: str, default=None):
        r = self.db.execute("SELECT value FROM state WHERE key=?", (key,)).fetchone()
        return json.loads(r[0]) if r else default

    def set_state(self, key: str, value) -> None:
        with self.db:
            self.db.execute("INSERT OR REPLACE INTO state VALUES (?,?)", (key, json.dumps(value)))

    # ---- retention
    def prune(self, now_ts: int | None = None) -> None:
        cutoff = int(now_ts if now_ts is not None else time.time()) - self.retention_s
        with self.db:
            for t in ("readings", "context", "embeddings", "runs"):
                self.db.execute(f"DELETE FROM {t} WHERE ts < ?", (cutoff,))
            self.db.execute("DELETE FROM alerts WHERE sent_ts IS NOT NULL AND created_ts < ?", (cutoff,))
