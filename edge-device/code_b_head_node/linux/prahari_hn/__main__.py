"""python -m prahari_hn --config <site.toml>"""
import argparse
import logging

from . import rpc
from .config import load_config
from .node import HeadNode


def main(argv=None):
    p = argparse.ArgumentParser(description="PRAHARI head node — Linux side runtime")
    p.add_argument("--config", required=True)
    p.add_argument("--once", action="store_true", help="run one inference cycle on stored history and exit")
    p.add_argument("-v", "--verbose", action="store_true")
    a = p.parse_args(argv)
    logging.basicConfig(level=logging.DEBUG if a.verbose else logging.INFO,
                        format="%(asctime)s %(levelname)s %(name)s: %(message)s")
    cfg = load_config(a.config)
    link = rpc.open_link(cfg.rpc_device, cfg.rpc_baud)
    node = HeadNode(cfg, link)
    if a.once:
        r = node.run_cycle()
        print({k: v for k, v in r.items() if k != "alerts"})
        return 0
    node.run_forever()


if __name__ == "__main__":
    raise SystemExit(main())
