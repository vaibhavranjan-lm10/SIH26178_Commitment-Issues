"""CLI: build an L0 electrical-stimulus bundle for one or more pod positions.

    python -m test_env.l0_stimulus.generate --out <dir> [--position G ...] [--dt 60]
                                            [--duration-h 120] [--resd]

`--resd` additionally converts the ADC / UART CSVs into Renode .resd files
using Renode's own tools/csv2resd (found via $RENODE_ROOT or --renode-root),
and drops a stimulus.resc snippet skeleton. Without it the bundle is still
complete -- the CSV / JSON / bin artifacts are the canonical form.
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

from .bundle import build_bundle
from .physical import VillageScenario
from .transducers import POSITIONS

VILLAGE_POSITIONS = ("S1", "G", "U")     # blueprint section 11.4: the village-profile column


def _csv2resd(renode_root: Path, args: list[str]) -> None:
    tool = renode_root / "tools" / "csv2resd" / "csv2resd.py"
    if not tool.exists():
        raise FileNotFoundError(f"csv2resd not found at {tool}")
    subprocess.run([sys.executable, str(tool), *args], check=True, capture_output=True, text=True)


def _emit_resd(bundle_dir: Path, manifest: dict, renode_root: Path, dt_s: float) -> None:
    resd_dir = bundle_dir / "resd"
    resd_dir.mkdir(exist_ok=True)
    freq = 1.0 / dt_s
    for ch in manifest["channels"]["adc"]:
        csv = bundle_dir / "adc" / f"{ch['name']}.csv"
        if csv.exists():
            _csv2resd(renode_root, ["-i", str(csv), "-m", "VOLTAGE:voltage", "-f", str(freq),
                                    str(resd_dir / f"adc_{ch['name']}.resd")])
    for ch in manifest["channels"]["uart"]:
        # BINARY_DATA RESD: one row per frame, size + hex data
        src = bundle_dir / "uart" / f"{ch['name']}.jsonl"
        rows = [json.loads(l) for l in src.read_text().splitlines() if l]
        tmp = resd_dir / f"uart_{ch['name']}.csv"
        with tmp.open("w") as f:
            f.write("timestamp,size,data\n")
            for r in rows:
                f.write(f"{r['t_ns']},{len(r['frame_hex'])//2},#{r['frame_hex']}\n")
        _csv2resd(renode_root, ["-i", str(tmp), "-m", "BINARY_DATA:size,data:size,data",
                                "-t", "timestamp", str(resd_dir / f"uart_{ch['name']}.resd")])
    (bundle_dir / "stimulus.resc").write_text(_RESC_SKELETON.format(
        position=manifest["position"]))


_RESC_SKELETON = """\
# Skeleton Renode script for a {position} pod L0 stimulus run.
# The real platform wiring lives in test_env/renode/ (separate deliverable);
# this only shows how the artifacts in this bundle attach.
#
#   $bin  = the compiled Code A ELF (west build ... nucleo_l053r8)
#   sysbus.eeprom  <- load eeprom.bin at its mapped base
#   sysbus.adc1    FeedSample <count> <channelIdx>  (schedule from adc/<name>.count.csv;
#                  RESD in resd/ is for RESD-capable ADC models -- STM32_ADC in
#                  Renode 1.17 takes raw counts via FeedSample)
#   sysbus.gpioPortA  rain_gauge: replay pulse/rain_gauge.json edges via ScheduleAction
#                     anemometer: drive pin from pulse/anemometer.json rate_intervals
#   the I2C stub responder answers each read with i2c/<name>.jsonl[response_hex]
#   the UART feeder streams uart/<name>.bin (or resd/uart_<name>.resd) at 9600
"""


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--position", action="append", choices=POSITIONS,
                    help="repeatable; default = the village-profile column S1, G, U")
    ap.add_argument("--dt", type=float, default=60.0, help="stimulus grid seconds (default: pod sample period)")
    ap.add_argument("--duration-h", type=float, default=120.0)
    ap.add_argument("--seed", type=int, default=11)
    ap.add_argument("--rs485-addr", type=int, default=1)
    ap.add_argument("--resd", action="store_true", help="also emit Renode .resd via tools/csv2resd")
    ap.add_argument("--renode-root", type=Path, default=Path(os.environ.get("RENODE_ROOT", "")))
    args = ap.parse_args(argv)

    positions = args.position or list(VILLAGE_POSITIONS)
    sc = VillageScenario(duration_h=args.duration_h, seed=args.seed)
    args.out.mkdir(parents=True, exist_ok=True)
    summary = {}
    for i, pos in enumerate(positions):
        bdir = args.out / f"pod_{pos}"
        m = build_bundle(bdir, pos, sc, dt_s=args.dt, rs485_addr=args.rs485_addr + i)
        if args.resd:
            root = args.renode_root if args.renode_root and str(args.renode_root) else _guess_renode_root()
            _emit_resd(bdir, m, root, args.dt)
        summary[pos] = {"dir": str(bdir), "adc": len(m["channels"]["adc"]),
                        "pulse": len(m["channels"]["pulse"]), "i2c": len(m["channels"]["i2c"]),
                        "uart": len(m["channels"]["uart"])}
        print(f"pod_{pos}: {summary[pos]}")
    (args.out / "bundles.json").write_text(json.dumps(summary, indent=2))
    return 0


def _guess_renode_root() -> Path:
    for c in (Path.home() / "renode_portable", Path("/opt/renode"), Path(shutil.which("renode") or "").parent):
        if (c / "tools" / "csv2resd" / "csv2resd.py").exists():
            return c
    raise FileNotFoundError("Renode root not found; pass --renode-root or set RENODE_ROOT")


if __name__ == "__main__":
    raise SystemExit(main())
