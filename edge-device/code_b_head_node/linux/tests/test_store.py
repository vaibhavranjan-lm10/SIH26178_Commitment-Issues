"""SQLite ring buffer: persistence across 'reboot', hourly grid, masking, queue, prune."""
import numpy as np

from prahari_hn.store import HOUR, Store


def test_readings_survive_reopen_and_hourly_grid_masks_gaps(tmp_path):
    p = tmp_path / "ring.sqlite"
    s = Store(p, retention_days=60)
    t0 = 1_757_400_000 // HOUR * HOUR
    for h in range(10):
        if h == 4:
            continue                                   # a missing hour: nothing written, nothing filled
        s.put_readings(t0 + h * HOUR + 120, {"P13": (20.0 + h, 0), "P12": (float(h), 0)})
        s.put_readings(t0 + h * HOUR + 1800, {"P13": (30.0 + h, 0)})   # a later sweep in the same hour
    s.close()
    s = Store(p, retention_days=60)                  # "reboot"
    assert s.integrity == "ok"
    vals, mask = s.hourly_matrix(["P13", "P12", "P99"], t0 + 9 * HOUR + 600, 10)
    assert vals.shape == (10, 3)
    assert mask[:, 2].sum() == 0                     # channel never reported → all masked
    assert mask[4, 0] == 0 and np.isnan(vals[4, 0])  # the gap is a gap
    assert vals[3, 0] == 33.0                        # last reading in the hour wins
    assert vals[9, 1] == 9.0 and mask[9, 1] == 1
    assert s.latest_reading_ts() == t0 + 9 * HOUR + 1800


def test_embeddings_and_neighbours(tmp_path):
    s = Store(tmp_path / "r.sqlite")
    e = np.arange(16, dtype=np.int8)
    for k in range(4):
        s.put_embedding(1000 + k * 3600, 7, e + k, own=False)
    s.put_embedding(1000, 1, e, own=True)
    h = s.embedding_history(7, periods=3, before_ts=1000 + 3 * 3600)
    assert [t for t, _ in h] == [1000 + 3600, 1000 + 7200, 1000 + 10800] and (h[-1][1] == e + 3).all()
    assert s.neighbours_seen(1000 + 10800) == [(7, 1000 + 10800)]
    assert s.neighbours_seen(1000 + 10801) == []


def test_alert_queue_and_prune(tmp_path):
    s = Store(tmp_path / "r.sqlite", retention_days=1)
    a = s.queue_alert(100, "FL_t24", 2, {"identifier": "x"})
    b = s.queue_alert(200, "FL_t24", 0, {"identifier": "y"})
    assert [i for i, _ in s.unsent_alerts()] == [a, b]
    s.mark_sent(a, 300)
    assert [i for i, _ in s.unsent_alerts()] == [b]
    s.put_readings(100, {"P1": (0.2, 0)}); s.put_readings(200_000, {"P1": (0.3, 0)})
    s.log_run(100, False, 0, "h", {"FL_t0": 0.1})
    s.prune(now_ts=200_000)
    assert s.db.execute("SELECT COUNT(*) FROM readings").fetchone()[0] == 1
    assert s.db.execute("SELECT COUNT(*) FROM runs").fetchone()[0] == 0
    assert [i for i, _ in s.unsent_alerts()] == [b]   # unsent alerts are never pruned
    assert s.db.execute("SELECT COUNT(*) FROM alerts").fetchone()[0] == 1


def test_state_round_trip(tmp_path):
    s = Store(tmp_path / "r.sqlite")
    assert s.get_state("duty_cycle") is None
    s.set_state("duty_cycle", {"mode": "elevated", "calm_streak": 2})
    s.close(); s = Store(tmp_path / "r.sqlite")
    assert s.get_state("duty_cycle") == {"mode": "elevated", "calm_streak": 2}
