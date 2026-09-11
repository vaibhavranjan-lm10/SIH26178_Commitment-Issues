"""What crosses the mesh (§7.5) and how it is interpreted.

The MCU side transmits the bytes this side hands it (``$EMB``) in its TDMA
slot and forwards every non-pod packet it hears as ``$NBR <hex>``; the
radio timing is entirely the MCU's.  This module owns the payload:

    byte 0    MAGIC 0xB7
    byte 1    version 1
    byte 2-3  node id, uint16 LE (FNV-1a of the node id string, low 16 bits)
    byte 4-19 embedding, 16 × int8 (scale from the model manifest)
    = 20 bytes: "16 B int8 embedding + 4 B header" (§7.3)

Only embeddings.  Never raw telemetry.  A packet that is not a valid mesh
packet is dropped.  Neighbour bookkeeping (rounds, lateness, adjacency
prior) is here too; the *learned* part of adjacency is inside the graph
ONNX (§7.4 "combined with a learned adaptive matrix").
"""
from __future__ import annotations

import math
import struct
from dataclasses import dataclass

import numpy as np

MESH_MAGIC = 0xB7
MESH_VERSION = 1
MESH_LEN = 20
EMB_DIM = 16
ADJ_PRIOR_SCALE_M = 3000.0      # distance prior e^(−d/3 km), as in the synthetic training corpus


def fnv1a16(s: str) -> int:
    h = 0x811C9DC5
    for b in s.encode("utf-8"):
        h ^= b
        h = (h * 0x01000193) & 0xFFFFFFFF
    return (h ^ (h >> 16)) & 0xFFFF


def encode_packet(node_id16: int, emb_int8: np.ndarray) -> bytes:
    e = np.asarray(emb_int8, dtype=np.int8)
    if e.shape != (EMB_DIM,):
        raise ValueError("embedding must be 16 int8")
    return struct.pack("<BBH", MESH_MAGIC, MESH_VERSION, node_id16 & 0xFFFF) + e.tobytes()


def decode_packet(buf: bytes) -> tuple[int, np.ndarray] | None:
    if len(buf) != MESH_LEN or buf[0] != MESH_MAGIC or buf[1] != MESH_VERSION:
        return None
    nid = struct.unpack_from("<H", buf, 2)[0]
    return nid, np.frombuffer(buf[4:], dtype=np.int8).copy()


def quantise(emb: np.ndarray, scale: float) -> np.ndarray:
    return np.clip(np.round(np.asarray(emb, dtype=np.float32) * scale), -127, 127).astype(np.int8)


def dequantise(q: np.ndarray, scale: float) -> np.ndarray:
    return np.asarray(q, dtype=np.float32) / scale


@dataclass
class NeighbourHistory:
    node_id16: int
    hist: np.ndarray          # (periods, 16) float, oldest → newest
    weight: float             # adjacency prior to this node
    last_seen_ts: int


def adjacency_prior(own_xy: tuple[float, float] | None, nbr_xy: tuple[float, float] | None) -> float:
    """§7.4 distance prior.  Unknown positions → a flat 0.5 (the learned matrix
    inside the graph stage does the rest)."""
    if own_xy is None or nbr_xy is None or nbr_xy[0] is None or nbr_xy[1] is None:
        return 0.5
    d = math.hypot(own_xy[0] - nbr_xy[0], own_xy[1] - nbr_xy[1])
    return float(math.exp(-d / ADJ_PRIOR_SCALE_M))


def build_graph_inputs(own_hist: np.ndarray, nbrs: list[NeighbourHistory], max_neighbours: int,
                       nbr_nbr_weight: dict[tuple[int, int], float] | None = None):
    """Fixed-shape inputs for graph.onnx.

    own_hist (P,16) float; nbrs already filtered to those that reported this
    round (late/missing ones are dropped by the caller — §7.4 step 6).
    Returns hist (1,N,16,P), adj (1,N,N), valid (1,N) with N = 1+max_neighbours,
    node 0 = self.  Neighbour–neighbour edges come from ``nbr_nbr_weight``
    when known (2-hop ego-network), else 0.
    """
    P = own_hist.shape[0]
    N = 1 + max_neighbours
    hist = np.zeros((1, N, EMB_DIM, P), np.float32)
    adj = np.zeros((1, N, N), np.float32)
    valid = np.zeros((1, N), np.float32)
    hist[0, 0] = own_hist.T
    valid[0, 0] = 1.0
    use = sorted(nbrs, key=lambda n: -n.weight)[:max_neighbours]
    for i, n in enumerate(use, start=1):
        hist[0, i] = n.hist.T
        valid[0, i] = 1.0
        adj[0, 0, i] = adj[0, i, 0] = n.weight
    if nbr_nbr_weight:
        for i, a in enumerate(use, start=1):
            for j, b in enumerate(use, start=1):
                if i != j:
                    w = nbr_nbr_weight.get((a.node_id16, b.node_id16)) or nbr_nbr_weight.get((b.node_id16, a.node_id16))
                    if w:
                        adj[0, i, j] = w
    return hist, adj, valid
