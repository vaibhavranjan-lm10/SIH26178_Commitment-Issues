"""Corpus → model-ready arrays: features, held-out-column split, global
normalisation, context windows, ego-networks and the embedding cache.

OFFLINE ONLY.
"""
from __future__ import annotations

from dataclasses import dataclass

import numpy as np
import pandas as pd
import torch
from torch.utils.data import Dataset

from . import features as F
from . import heads as H
from . import model as M
from .synthetic import Corpus


@dataclass
class ColumnData:
    cid: str
    profile: str
    values: np.ndarray   # (T, 41) float32, 0 where masked
    mask: np.ndarray     # (T, 41) float32
    labels: np.ndarray   # (T, 15) float32
    index: pd.DatetimeIndex


def prepare_columns(corpus: Corpus) -> list[ColumnData]:
    out = []
    for col in corpus.columns:
        feats = F.compute_features(col.primaries, col.site.static)
        vals, mask = F.assemble_trained_channels(col.primaries, feats, col.site.static)
        out.append(ColumnData(col.site.cid, col.site.profile, vals.to_numpy(np.float32), mask.to_numpy(np.float32),
                              col.labels[H.OUTPUT_NAMES].to_numpy(np.float32), col.primaries.index))
    return out


def split_by_column(columns: list[ColumnData], val_frac: float = 0.25, seed: int = 0,
                    n_tries: int = 64, min_pos: int = 10) -> tuple[list[int], list[int]]:
    """Held-out-COLUMN validation split.

    Why not a temporal cut within each column?  Events are injected (and in
    the real world, occur) as a handful of episodes per site with slow
    recessions; a temporal tail of one column can easily contain
    zero positives for exactly the hazard the split is meant to test, or contain
    only the recession of an event whose onset was in the training part —
    leaking the event and making the PR-curve threshold meaningless.  Holding
    out whole columns gives the validation set complete, unseen episodes at
    unseen sites, which is also what deployment looks like (a new site
    running globally trained weights, blueprint §8.4).

    Profiles are stratified so validation is not, e.g., all-forest, and
    among ``n_tries`` stratified candidates the one whose validation set
    covers the most hazard outputs with ≥ ``min_pos`` positive hours is
    kept (ties → larger minimum positive count).  A held-out set with no
    fire positives cannot calibrate the fire head; the corpus is checked
    for that here rather than discovered at threshold time.
    """
    rng = np.random.default_rng(seed)
    n_val = max(1, int(round(val_frac * len(columns))))
    trained_cols = [k for k, n in enumerate(H.OUTPUT_NAMES) if H.BY_CODE[n.split("_")[0]].trained]
    pos = np.stack([c.labels.sum(0) for c in columns])          # (n_columns, 15)

    def candidate() -> list[int]:
        by_profile: dict[str, list[int]] = {}
        for i, c in enumerate(columns):
            by_profile.setdefault(c.profile, []).append(i)
        profiles = list(by_profile)
        rng.shuffle(profiles)
        val: list[int] = []
        while len(val) < n_val:
            for p in profiles:
                if by_profile[p] and len(val) < n_val:
                    val.append(by_profile[p].pop(int(rng.integers(len(by_profile[p])))))
        return sorted(val)

    best, best_key = None, None
    for _ in range(n_tries):
        val = candidate()
        vp = pos[val].sum(0)[trained_cols]
        present = pos.sum(0)[trained_cols] > 0                    # outputs the corpus has at all
        key = (int(((vp >= min_pos) & present).sum()), float(vp[present].min()) if present.any() else 0.0)
        if best_key is None or key > best_key:
            best, best_key = val, key
    train = [i for i in range(len(columns)) if i not in best]
    return sorted(train), sorted(best)


def normalisation_stats(columns: list[ColumnData]) -> tuple[np.ndarray, np.ndarray]:
    """Global (all training columns pooled) per-channel mean/std over OBSERVED
    values only.  This is the choice made for open decision 4 (global vs
    per-site): global, because per-site adaptation is specified as a
    fine-tune of the static embedding, not of the input scaling (§8.4)."""
    v = np.concatenate([c.values for c in columns], 0)
    m = np.concatenate([c.mask for c in columns], 0)
    cnt = m.sum(0).clip(min=1)
    mean = (v * m).sum(0) / cnt
    var = (((v - mean) ** 2) * m).sum(0) / cnt
    std = np.sqrt(var)
    std = np.where(std < 1e-6, 1.0, std)
    return mean.astype(np.float32), std.astype(np.float32)


class WindowDataset(Dataset):
    """(column, t) samples: context window ending at t, target labels at t."""

    def __init__(self, columns: list[ColumnData], col_ids: list[int], context: int = M.CONTEXT_LENGTH,
                 stride: int = 1, t_min: int | None = None):
        self.columns, self.context = columns, context
        t0 = max(context - 1, t_min or 0)
        self.items = [(c, t) for c in col_ids for t in range(t0, len(columns[c].index), stride)]

    def __len__(self):
        return len(self.items)

    def __getitem__(self, i):
        c, t = self.items[i]
        col = self.columns[c]
        sl = slice(t - self.context + 1, t + 1)
        return (torch.from_numpy(col.values[sl]), torch.from_numpy(col.mask[sl]),
                torch.from_numpy(col.labels[t]), c, t)


# ---------------------------------------------------------------- graph
def ego_networks(corpus: Corpus, max_nbrs: int = M.MAX_NEIGHBOURS) -> dict[int, tuple[list[int], np.ndarray]]:
    """{column: (node list with self first, adjacency among them)} — the K=2
    ego-network (§7.3 hops K=2), capped at ``max_nbrs`` by 1-hop-first, then
    weight."""
    A = corpus.adjacency()
    n = A.shape[0]
    out = {}
    for i in range(n):
        hop1 = [(j, A[i, j]) for j in range(n) if A[i, j] > 0]
        hop2 = {}
        for j, _ in hop1:
            for k in range(n):
                if A[j, k] > 0 and k != i and A[i, k] == 0:
                    hop2[k] = max(hop2.get(k, 0), A[i, j] * A[j, k])
        hop1.sort(key=lambda x: -x[1])
        ordered = [j for j, _ in hop1] + [k for k, _ in sorted(hop2.items(), key=lambda x: -x[1])]
        nodes = [i] + ordered[:max_nbrs]
        out[i] = (nodes, A[np.ix_(nodes, nodes)].astype(np.float32))
    return out


@torch.no_grad()
def embed_all(model: M.PrahariModel, columns: list[ColumnData], batch: int = 64,
              context: int = M.CONTEXT_LENGTH) -> np.ndarray:
    """Encoder embeddings for every (column, t) — the deployment-time cache of
    each cycle's own embedding.  NaN for t < context−1."""
    model.eval()
    T = len(columns[0].index)
    out = np.full((len(columns), T, M.EMB_DIM), np.nan, dtype=np.float32)
    for c, col in enumerate(columns):
        ts = list(range(context - 1, T))
        for s in range(0, len(ts), batch):
            tt = ts[s: s + batch]
            x = torch.from_numpy(np.stack([col.values[t - context + 1: t + 1] for t in tt]))
            m = torch.from_numpy(np.stack([col.mask[t - context + 1: t + 1] for t in tt]))
            out[c, tt] = model.embed(x, m).numpy()
    return out


class GraphSampleDataset(Dataset):
    """Samples for the graph stage from the embedding cache.

    Mimics deployment: node 0 = self with its last ``periods`` embeddings,
    neighbours are what arrived (each dropped with ``nbr_dropout`` — §7.4
    "a late or missing neighbour is dropped for that round").  Returns a
    fixed-shape (1+MAX_NEIGHBOURS) padded sample plus validity.
    """

    def __init__(self, emb_cache: np.ndarray, columns: list[ColumnData], col_ids: list[int],
                 ego: dict[int, tuple[list[int], np.ndarray]], periods: int = M.GRAPH_PERIODS,
                 stride: int = 1, nbr_dropout: float = 0.2, seed: int = 0, context: int = M.CONTEXT_LENGTH):
        self.emb, self.columns, self.ego, self.P = emb_cache, columns, ego, periods
        self.nbr_dropout = nbr_dropout
        self.rng = np.random.default_rng(seed)
        t0 = context - 1 + periods
        self.items = [(c, t) for c in col_ids for t in range(t0, len(columns[c].index), stride)]
        self.N = 1 + M.MAX_NEIGHBOURS

    def __len__(self):
        return len(self.items)

    def __getitem__(self, i):
        c, t = self.items[i]
        nodes, adj = self.ego[c]
        hist = np.zeros((self.N, M.EMB_DIM, self.P), np.float32)
        A = np.zeros((self.N, self.N), np.float32)
        valid = np.zeros(self.N, np.float32)
        keep = [0] + [k for k in range(1, len(nodes)) if self.rng.random() >= self.nbr_dropout]
        for a, k in enumerate(keep):
            hist[a] = self.emb[nodes[k], t - self.P + 1: t + 1].T
            valid[a] = 1.0
        for a, ka in enumerate(keep):
            for b, kb in enumerate(keep):
                A[a, b] = adj[ka, kb]
        y = self.columns[c].labels[t]
        return (torch.from_numpy(self.emb[c, t].copy()), torch.from_numpy(hist), torch.from_numpy(A),
                torch.from_numpy(valid), torch.from_numpy(y), c, t)
