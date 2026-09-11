"""Assemble a per-position stimulus bundle Renode can feed to Code A.

A bundle is a directory holding, for one pod position over the whole
scenario:

  eeprom.bin                 the pod config image (eeprom.py): position,
                             link identity, and the calibration table that
                             inverts this bundle's ADC transfer functions
  adc/<name>.csv             per channel, `timestamp_ns,voltage` (microvolts)
                             -- csv2resd input for a VOLTAGE RESD block
  adc/<name>.count.csv       the same series as raw 12-bit counts, for
                             Renode's STM32_ADC.FeedSample(value, channel)
  adc/vrefint.count.csv      channel 17, the pod's own-rail self-report
  pulse/<name>.json          GPIO edge train: port, pin, idle level, and
                             [[t_ns, level], ...]
  i2c/<name>.jsonl           one line per acquisition cycle:
                             {t_ns, addr, response_hex, decoded:{...}}
  uart/<name>.bin            concatenated sensor frames
  uart/<name>.jsonl          {t_ns, frame_hex, decoded:{...}} per cycle
  manifest.json              scenario params, dt, channel map, and the
                             electrical envelope every artifact was checked
                             against (also what the tests assert)

This module writes only the canonical electrical representation. Turning
the CSVs into .resd (via Renode's own tools/csv2resd) and wiring pins to
peripherals is renode_glue.py / the future test_env/renode/ harness.
"""
from __future__ import annotations

import json
from dataclasses import asdict
from pathlib import Path

from . import transducers as T
from .eeprom import build_eeprom_image, calib_entries_for
from .physical import VillageScenario, physical_series

SAMPLE_PERIOD_S_DEFAULT = 60          # CONFIG_PRAHARI_SAMPLE_PERIOD_S default


def _adc_series(ch: T.AdcChannel, states, dt_s):
    rows_uv, rows_cnt = [], []
    for k, st in enumerate(states):
        phys = getattr(st, ch.quantity)
        v = ch.to_volts(phys)
        if not (ch.v_min - 1e-6 <= v <= ch.v_max + 1e-6):
            raise ValueError(f"{ch.name}: conditioned signal {v:.4f} V outside "
                             f"front-end swing [{ch.v_min}, {ch.v_max}] at t={k*dt_s}s")
        count = T.volts_to_count(v)
        t_ns = int(k * dt_s * 1e9)
        rows_uv.append((t_ns, T.count_to_uv(count)))
        rows_cnt.append((t_ns, count))
    return rows_uv, rows_cnt


def build_bundle(out_dir: str | Path, position: str, scenario: VillageScenario | None = None,
                 dt_s: float = SAMPLE_PERIOD_S_DEFAULT, rs485_addr: int = 1, lora_slot: int = 0,
                 rail_mv: float = 3300.0) -> dict:
    sc = scenario or VillageScenario()
    out = Path(out_dir)
    (out / "adc").mkdir(parents=True, exist_ok=True)
    (out / "pulse").mkdir(exist_ok=True)
    (out / "i2c").mkdir(exist_ok=True)
    (out / "uart").mkdir(exist_ok=True)

    states = physical_series(sc, dt_s)
    t_end_s = (len(states) - 1) * dt_s
    tx = T.transducers_at(position)
    manifest: dict = {
        "kind": "PRAHARI L0 electrical stimulus (Renode) -- NOT application-level data",
        "position": position, "scenario": "blueprint section 11 village worked example",
        "duration_h": sc.duration_h, "dt_s": dt_s, "n_samples": len(states),
        "rs485_addr": rs485_addr, "lora_slot": lora_slot,
        "electrical_envelope": {
            "adc_counts_max": T.ADC_COUNTS_MAX, "adc_vref_uv": T.ADC_VREF_UV,
            "gpio_levels": [0, 1], "i2c_byte_range": [0, 255], "uart_byte_range": [0, 255],
        },
        "channels": {"adc": [], "pulse": [], "i2c": [], "uart": []},
        # section 11.4 transducers the current overlay does not populate:
        # modelled in physical.py, but no peripheral to feed (see README).
        "unrealised_channels": {
            "P11_water_level": {"physical": "stage_m", "reason": "no P11 transducer in nucleo_l053r8.overlay"},
            "P38_turbidity": {"physical": None, "reason": "no P38 transducer in nucleo_l053r8.overlay"},
        },
    }

    # ---- EEPROM (position + link + calibration matched to the ADC transfer fns)
    (out / "eeprom.bin").write_bytes(build_eeprom_image(position, rs485_addr, lora_slot))
    manifest["calibration"] = [asdict(e) for e in calib_entries_for(position)]

    # ---- class C: ADC
    for ch in tx["adc"]:
        uv, cnt = _adc_series(ch, states, dt_s)
        _write_csv(out / "adc" / f"{ch.name}.csv", "timestamp,voltage", uv)
        _write_csv(out / "adc" / f"{ch.name}.count.csv", "timestamp,count", cnt)
        manifest["channels"]["adc"].append({
            "name": ch.name, "adc_channel": ch.channel, "param": ch.param,
            "quantity": ch.quantity, "front_end_volts": [ch.v_min, ch.v_max],
            "count_min": min(c for _, c in cnt), "count_max": max(c for _, c in cnt),
        })
    # own-rail self-report (VREFINT, channel 17) -- always present
    vref = [(int(k * dt_s * 1e9), T.vrefint_count(rail_mv)) for k in range(len(states))]
    _write_csv(out / "adc" / "vrefint.count.csv", "timestamp,count", vref)
    manifest["channels"]["adc"].append({
        "name": "vrefint", "adc_channel": T.VREFINT_CHANNEL, "param": "O1 (rail self-report)",
        "count_min": vref[0][1], "count_max": vref[0][1], "rail_mv": rail_mv})

    # ---- class D: pulse
    #   sparse discrete events (rain tips: exact count == accumulation) -> enumerate
    #     every edge, with contact chatter, so the debounce path is exercised.
    #   dense trains (anemometer: many Hz for days) -> a piecewise-constant
    #     frequency schedule; enumerating millions of edges is neither loadable
    #     nor informative. A short sample_edges burst shows the concrete shape.
    for ch in tx["pulse"]:
        entry = {"name": ch.name, "port": ch.port, "pin": ch.pin, "param": ch.param,
                 "idle_level": ch.idle_level, "active_level": ch.active_level,
                 "debounce_ms": ch.debounce_ms}
        if ch.name == "rain_gauge":
            times = T.rain_tip_times_s(states, dt_s)
            edges = T.build_pulse_edges(ch, times, t_end_s)
            entry.update(representation="edges", n_pulses=len(times),
                         edges=[[ns, lvl] for ns, lvl in edges])
            mrec = {"name": ch.name, "param": ch.param, "representation": "edges",
                    "n_pulses": len(times), "n_edges": len(edges)}
        else:  # anemometer
            iv = T.anemometer_rate_intervals(states, dt_s)
            sample = T.build_pulse_edges(ch, T.anemometer_pulse_times_s(states[:4], dt_s), 3 * dt_s)
            entry.update(representation="rate_intervals", unit="Hz",
                         intervals=iv, sample_edges=[[ns, lvl] for ns, lvl in sample])
            mrec = {"name": ch.name, "param": ch.param, "representation": "rate_intervals",
                    "n_intervals": len(iv), "hz_min": min(h for _, h in iv),
                    "hz_max": max(h for _, h in iv)}
        (out / "pulse" / f"{ch.name}.json").write_text(json.dumps(entry, indent=1))
        manifest["channels"]["pulse"].append(mrec)

    # ---- class A: I2C
    for s in tx["i2c"]:
        lines = []
        for k, st in enumerate(states):
            t_ns = int(k * dt_s * 1e9)
            if s.kind == "sht4x":
                fr = T.sht4x_frame(st.air_temp_c, st.rh_pct)
                dec = dict(zip(("temp_c", "rh_pct"), (round(x, 3) for x in T.sht4x_decode(fr))))
            elif s.kind == "lps22hb":
                fr = T.lps22hb_frame(st.pressure_hpa, st.air_temp_c)
                dec = dict(zip(("pressure_hpa", "temp_c"), (round(x, 3) for x in T.lps22hb_decode(fr))))
            elif s.kind == "lis2dh":
                fr = T.lis2dh_frame(st.tilt_mg)
                dec = {"mg_xyz": [round(x, 2) for x in T.lis2dh_decode(fr)]}
            else:
                continue
            lines.append(json.dumps({"t_ns": t_ns, "addr": s.addr,
                                     "response_hex": fr.hex(), "decoded": dec}))
        (out / "i2c" / f"{s.name}.jsonl").write_text("\n".join(lines) + "\n")
        manifest["channels"]["i2c"].append({"name": s.name, "addr": s.addr,
                                            "params": list(s.params), "kind": s.kind})

    # ---- class B: UART
    for s in tx["uart"]:
        blob = bytearray()
        lines = []
        for k, st in enumerate(states):
            t_ns = int(k * dt_s * 1e9)
            fr = T.pms7003_frame(st.pm1_ugm3, st.pm25_ugm3, st.pm10_ugm3)
            blob += fr
            dec = dict(zip(("pm1", "pm2_5", "pm10"), T.pms7003_decode(fr)))
            lines.append(json.dumps({"t_ns": t_ns, "frame_hex": fr.hex(), "decoded": dec}))
        (out / "uart" / f"{s.name}.bin").write_bytes(bytes(blob))
        (out / "uart" / f"{s.name}.jsonl").write_text("\n".join(lines) + "\n")
        manifest["channels"]["uart"].append({"name": s.name, "baud": s.baud,
                                             "params": list(s.params), "frame_bytes": 32})

    (out / "manifest.json").write_text(json.dumps(manifest, indent=2))
    return manifest


def _write_csv(path: Path, header: str, rows):
    with path.open("w") as f:
        f.write(header + "\n")
        for a, b in rows:
            f.write(f"{a},{b}\n")
