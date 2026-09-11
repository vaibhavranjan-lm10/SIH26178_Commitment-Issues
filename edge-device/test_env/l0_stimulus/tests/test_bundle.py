"""The written bundle -- files on disk Renode consumes -- must itself carry
only electrically valid values, and the manifest must state the envelope
they were checked against."""
import json

import pytest

from test_env.l0_stimulus import transducers as T
from test_env.l0_stimulus.bundle import build_bundle
from test_env.l0_stimulus.physical import VillageScenario


@pytest.fixture(scope="module")
def bundle(tmp_path_factory):
    d = tmp_path_factory.mktemp("l0")
    sc = VillageScenario(duration_h=48.0)          # shorter for test speed
    manifests = {pos: build_bundle(d / f"pod_{pos}", pos, sc, dt_s=60) for pos in ("S1", "G", "U")}
    return d, manifests


def test_adc_csv_values_all_in_electrical_range(bundle):
    d, _ = bundle
    for csv in d.rglob("adc/*.count.csv"):
        for line in csv.read_text().splitlines()[1:]:
            _, count = line.split(",")
            assert 0 <= int(count) <= T.ADC_COUNTS_MAX, csv
    for csv in d.rglob("adc/*.csv"):
        if csv.name.endswith(".count.csv"):
            continue
        for line in csv.read_text().splitlines()[1:]:
            ts, uv = line.split(",")
            assert 0 <= int(uv) <= T.ADC_VREF_UV, csv
            assert int(ts) >= 0


def test_pulse_json_levels_binary_and_edges_ordered(bundle):
    d, _ = bundle
    seen = set()
    for pj in d.rglob("pulse/*.json"):
        obj = json.loads(pj.read_text())
        seen.add(obj["name"])
        assert obj["idle_level"] in (0, 1) and obj["active_level"] in (0, 1)
        if obj["representation"] == "edges":
            edges = obj["edges"]
        else:  # rate_intervals: check the schedule, and the concrete sample burst
            assert obj["unit"] == "Hz"
            ts = [t for t, _ in obj["intervals"]]
            hz = [h for _, h in obj["intervals"]]
            assert ts == sorted(ts) and all(t >= 0 for t in ts)
            assert all(0.0 <= h <= 500.0 for h in hz)              # capped so 1/hz stays > debounce
            assert 1000.0 / max(h for h in hz if h > 0) >= 2.0     # min implied gap > 2 ms floor
            edges = obj["sample_edges"]
        ns = [e[0] for e in edges]
        lv = [e[1] for e in edges]
        assert set(lv) <= {0, 1}
        assert ns == sorted(ns)
        assert edges[0][1] == obj["idle_level"] and edges[-1][1] == obj["idle_level"]
    assert {"rain_gauge", "anemometer"} <= seen


def test_i2c_jsonl_frames_are_bytes_and_decode_reported(bundle):
    d, _ = bundle
    for jl in d.rglob("i2c/*.jsonl"):
        for line in jl.read_text().splitlines():
            r = json.loads(line)
            b = bytes.fromhex(r["response_hex"])
            assert all(0 <= x <= 255 for x in b) and 0 < r["addr"] < 128
            assert "decoded" in r and r["t_ns"] >= 0


def test_uart_bin_is_whole_frames_starting_with_the_pms_header(bundle):
    d, _ = bundle
    for b in d.rglob("uart/*.bin"):
        data = b.read_bytes()
        assert len(data) % 32 == 0 and data[0] == 0x42 and data[1] == 0x4D


def test_eeprom_bin_present_and_sized(bundle):
    d, _ = bundle
    for pos in ("S1", "G", "U"):
        img = (d / f"pod_{pos}" / "eeprom.bin").read_bytes()
        assert len(img) == 512 and img[0:2] == b"PD"


def test_manifest_declares_the_electrical_envelope(bundle):
    _, manifests = bundle
    for pos, m in manifests.items():
        env = m["electrical_envelope"]
        assert env["adc_counts_max"] == 4095 and env["adc_vref_uv"] == 3_300_000
        assert env["gpio_levels"] == [0, 1]
        assert "NOT application-level data" in m["kind"]
        for ch in m["channels"]["adc"]:
            assert 0 <= ch["count_min"] <= ch["count_max"] <= 4095


def test_build_bundle_raises_if_a_transfer_function_leaves_the_front_end(monkeypatch, tmp_path):
    """Guard the guard: if a transfer function is ever changed so it demands a
    voltage outside the channel's declared swing, the build must fail loudly,
    not write a silently-wrong CSV."""
    ch = next(c for c in T.ADC_CHANNELS if c.name == "pore_pressure")
    monkeypatch.setattr(ch, "to_volts", lambda kpa: 9.9)   # impossible pin voltage
    with pytest.raises(ValueError):
        build_bundle(tmp_path / "bad", "G", VillageScenario(duration_h=2.0), dt_s=60)


def test_generator_does_not_depend_on_the_training_pipeline():
    import test_env.l0_stimulus as pkg
    src = "".join((__import__("pathlib").Path(pkg.__file__).parent / f).read_text()
                  for f in ("physical.py", "transducers.py", "eeprom.py", "bundle.py", "generate.py"))
    assert "training_pipeline" not in src
    assert "prahari_train" not in src
