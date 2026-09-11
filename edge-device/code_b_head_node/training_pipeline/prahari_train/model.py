"""The model: Granite TTM encoder → optional A3TGCN graph stage → nine hazard
heads → per-head temperature scaling.  Blueprint §6.7 stages 1–5.

    stage 1  normalise (global z-score from the training corpus — the
             choice made here for open decision 4; stored in the artifact),
             mask missing channels (values → 0, parallel mask channels).
    stage 2  Granite TinyTimeMixer r2 (ibm-granite/granite-timeseries-ttm-r2,
             the real pretrained weights from the HF hub; channel-independent
             backbone, context 512) → patch-mean pooled → linear → 16-dim
             embedding.  ~0.77 M parameters incl. the projection ("~1 M").
    stage 3  OPTIONAL graph refinement: torch_geometric_temporal.A3TGCN over
             the K=2 ego-network of received neighbour embeddings, over the
             last `periods` exchange rounds, producing a residual added to
             the node's own embedding.  Wired ONLY to the four spatially
             propagating heads FL, UF, FI, PO (blueprint §9).  Skippable:
             the standalone path (stages 1, 2, 4, 5) never touches it.
    stage 4  nine heads, one small MLP each (~5 k params): 15 logits in the
             fixed order heads.OUTPUT_NAMES.  GL and WQ heads exist, run,
             and are exported, but are excluded from the loss (declared-only).
    stage 5  temperature scaling — nine scalars, one per head, fitted on
             validation logits after training (calibrate.py).

Both external dependencies are the genuine packages (granite-tsfm 0.3.9,
torch_geometric_temporal 0.56.2).  The only re-expression is
``DenseGraphStage``, an EXPORT-ONLY dense-adjacency rewrite of A3TGCN with
copied weights, because a fixed-shape dense graph is what the on-device
ONNX runtime can consume; tests assert it matches A3TGCN to 1e-5.

OFFLINE ONLY: this file defines and trains the network; only the exported
ONNX leaves this directory.
"""
from __future__ import annotations

import math

import torch
import torch.nn as nn
import torch.nn.functional as Fnn

from . import heads as H
from . import registry

TTM_MODEL_ID = "ibm-granite/granite-timeseries-ttm-r2"
CONTEXT_LENGTH = 512          # TTM r2 512-96 variant; 512 h ≈ 21 days at the hourly cadence
EMB_DIM = 16                  # §7.5: 16-byte embedding crosses the mesh
GRAPH_PERIODS = 3             # exchange rounds the graph stage looks back over
MAX_NEIGHBOURS = 8            # fixed-shape export: at most this many 2-hop neighbours
EMB_INT8_SCALE = 16.0         # int8 = round(clip(emb × 16, −127, 127)); range ±7.9

# TTM r2 architecture (config.json of the hub model) — used when building
# without the pretrained weights (tests / offline smoke runs).
_TTM_R2_ARCH = dict(context_length=512, prediction_length=96, patch_length=64, patch_stride=64,
                    d_model=192, num_layers=2, expansion_factor=2, mode="common_channel", gated_attn=True,
                    norm_mlp="LayerNorm", scaling="std", adaptive_patching_levels=3, use_positional_encoding=False,
                    positional_encoding_type="sincos", self_attn=False, use_decoder=True, decoder_num_layers=2,
                    decoder_d_model=128, decoder_mode="common_channel", dropout=0.4, head_dropout=0.4)


# ---------------------------------------------------------------- stage 2
class TTMEncoder(nn.Module):
    """Granite TTM backbone + projection → 16-dim embedding.

    Input channels = 2 × n_channels: the masked, normalised values followed
    by the mask itself, so "missing" is an explicit signal, never a
    fabricated value.

    TTM's own per-window standard scaler is DISABLED (``scaling=None``):
    inputs are already globally z-scored (stage 1), and a per-window scaler
    would (a) erase every channel that is constant within the window — the
    whole static site block P70/P75/P77/S30/S33/S35, which the spec says is
    "what lets globally trained weights localise" — and (b) divide those
    constant channels by its 1e-5 variance floor, amplifying float rounding
    of the window mean into ~1e-4 noise that differs between torch and
    onnxruntime.  Both were observed before this was switched off.
    """

    def __init__(self, n_channels: int, pretrained: bool = True, dropout: float = 0.1):
        super().__init__()
        from tsfm_public.models.tinytimemixer import (TinyTimeMixerConfig, TinyTimeMixerForPrediction,
                                                      TinyTimeMixerModel)
        self.n_channels = n_channels
        self.n_in = 2 * n_channels
        if pretrained:
            full = TinyTimeMixerForPrediction.from_pretrained(
                TTM_MODEL_ID, num_input_channels=self.n_in, dropout=dropout, head_dropout=dropout, scaling=None)
            self.backbone = full.backbone
            cfg = full.config
            self.pretrained_source = TTM_MODEL_ID
        else:
            cfg = TinyTimeMixerConfig(num_input_channels=self.n_in,
                                      **{**_TTM_R2_ARCH, "dropout": dropout, "head_dropout": dropout, "scaling": None})
            self.backbone = TinyTimeMixerModel(cfg)
            self.pretrained_source = "RANDOM-INIT (no pretrained weights; tests/smoke only)"
        self.context_length = cfg.context_length
        self.d_model = cfg.d_model
        self.proj = nn.Linear(self.n_in * cfg.d_model, EMB_DIM)

    def forward(self, x_norm: torch.Tensor, mask: torch.Tensor) -> torch.Tensor:
        # x_norm, mask: (B, L, C)
        inp = torch.cat([x_norm * mask, mask], dim=-1)
        h = self.backbone(past_values=inp).last_hidden_state  # (B, 2C, n_patches, d)
        h = h.mean(dim=2).flatten(1)
        return self.proj(h)


# ---------------------------------------------------------------- stage 3
class GraphStage(nn.Module):
    """A3TGCN over an ego-network; returns a residual refinement per node.
    Output linear is zero-initialised so an untrained stage is a no-op."""

    def __init__(self, periods: int = GRAPH_PERIODS):
        super().__init__()
        from torch_geometric_temporal.nn.recurrent import A3TGCN
        self.periods = periods
        self.a3t = A3TGCN(in_channels=EMB_DIM, out_channels=EMB_DIM, periods=periods)
        self.out = nn.Linear(EMB_DIM, EMB_DIM)
        nn.init.zeros_(self.out.weight)
        nn.init.zeros_(self.out.bias)

    def forward(self, X: torch.Tensor, edge_index: torch.Tensor, edge_weight: torch.Tensor) -> torch.Tensor:
        # X: (N_total, 16, periods) — node embedding histories, oldest → newest
        return self.out(self.a3t(X, edge_index, edge_weight))


def build_batch_graph(samples: list[tuple[torch.Tensor, torch.Tensor, torch.Tensor]]):
    """Batch ego-graphs for GraphStage.

    Each sample is (hist (N_i, 16, P), adj (N_i, N_i), _) with node 0 = the
    node itself.  Returns X (ΣN_i, 16, P), edge_index, edge_weight, root_idx.
    Undirected edges are emitted in both directions; no self-loops (GCNConv
    adds them).
    """
    xs, ei, ew, roots, off = [], [], [], [], 0
    for hist, adj, _ in samples:
        n = hist.shape[0]
        xs.append(hist)
        roots.append(off)
        idx = torch.nonzero(adj > 0, as_tuple=False)
        if idx.numel():
            ei.append(idx.t() + off)
            ew.append(adj[idx[:, 0], idx[:, 1]])
        off += n
    X = torch.cat(xs, 0)
    if ei:
        edge_index = torch.cat(ei, 1).long()
        edge_weight = torch.cat(ew, 0).float()
    else:
        edge_index = torch.zeros(2, 0, dtype=torch.long)
        edge_weight = torch.zeros(0)
    return X, edge_index, edge_weight, torch.tensor(roots, dtype=torch.long)


class DenseGraphStage(nn.Module):
    """EXPORT-ONLY dense re-expression of GraphStage (A3TGCN → TGCN → GCNConv).

    Weights are *copied* from the trained GraphStage; nothing is trained
    here.  Semantics reproduced exactly from the installed
    torch_geometric_temporal 0.56.2 source: A3TGCN applies one TGCN step per
    period with H = 0 and sums them with softmax attention; GCNConv with
    add_self_loops/normalize is  Â X W + b,  Â = D^-½ (A + I) D^-½.
    Invalid (padding) nodes are isolated so they cannot influence node 0.
    """

    def __init__(self, gs: GraphStage):
        super().__init__()
        t = gs.a3t._base_tgcn
        self.periods = gs.periods
        self.register_buffer("att", torch.softmax(gs.a3t._attention.detach(), 0).clone())
        for name, conv in (("z", t.conv_z), ("r", t.conv_r), ("h", t.conv_h)):
            self.register_buffer(f"W{name}", conv.lin.weight.detach().clone())      # (out, in)
            self.register_buffer(f"b{name}", conv.bias.detach().clone())
        for name, lin in (("z", t.linear_z), ("r", t.linear_r), ("h", t.linear_h)):
            self.register_buffer(f"LW{name}", lin.weight.detach().clone())          # (out, 2*out)
            self.register_buffer(f"Lb{name}", lin.bias.detach().clone())
        self.register_buffer("OW", gs.out.weight.detach().clone())
        self.register_buffer("Ob", gs.out.bias.detach().clone())

    def forward(self, X: torch.Tensor, A: torch.Tensor, valid: torch.Tensor) -> torch.Tensor:
        # X (B, N, 16, P); A (B, N, N) symmetric, no self-loops; valid (B, N) with valid[:,0] = 1
        B, N = valid.shape
        vm = valid[:, :, None] * valid[:, None, :]
        eye = torch.eye(N, dtype=X.dtype, device=X.device)[None]
        A_hat = A * vm * (1 - eye) + eye                   # + I on every node (isolates invalid ones)
        deg = A_hat.sum(-1)
        dinv = deg.pow(-0.5)
        A_norm = dinv[:, :, None] * A_hat * dinv[:, None, :]
        zeros = torch.zeros(B, N, EMB_DIM, dtype=X.dtype, device=X.device)
        H_acc = zeros
        for p in range(self.periods):
            Xp = X[..., p]
            cz = torch.matmul(A_norm, torch.matmul(Xp, self.Wz.t())) + self.bz
            ch = torch.matmul(A_norm, torch.matmul(Xp, self.Wh.t())) + self.bh
            Z = torch.sigmoid(torch.matmul(torch.cat([cz, zeros], -1), self.LWz.t()) + self.Lbz)
            Ht = torch.tanh(torch.matmul(torch.cat([ch, zeros], -1), self.LWh.t()) + self.Lbh)
            H_acc = H_acc + self.att[p] * ((1 - Z) * Ht)      # H = 0 ⇒ Z·H term vanishes, R unused
        root = H_acc[:, 0]
        return torch.matmul(root, self.OW.t()) + self.Ob


# ---------------------------------------------------------------- stage 4 + 5
class HazardHeads(nn.Module):
    def __init__(self, hidden: int = 64):
        super().__init__()
        self.mlps = nn.ModuleDict({
            h.code: nn.Sequential(nn.Linear(EMB_DIM, hidden), nn.GELU(), nn.Linear(hidden, hidden), nn.GELU(),
                                  nn.Linear(hidden, h.n_outputs))
            for h in H.HEADS})

    def forward(self, emb: torch.Tensor, emb_graph: torch.Tensor | None = None) -> torch.Tensor:
        outs = []
        for h in H.HEADS:
            src = emb_graph if (h.graph and emb_graph is not None) else emb
            outs.append(self.mlps[h.code](src))
        return torch.cat(outs, -1)                          # (B, 15) in heads.OUTPUT_NAMES order


class PrahariModel(nn.Module):
    def __init__(self, n_channels: int | None = None, pretrained: bool = True, periods: int = GRAPH_PERIODS,
                 dropout: float = 0.1):
        super().__init__()
        self.channels = registry.trained_channels()
        n_channels = n_channels or len(self.channels)
        assert n_channels == 41, n_channels
        self.register_buffer("norm_mean", torch.zeros(n_channels))
        self.register_buffer("norm_std", torch.ones(n_channels))
        self.encoder = TTMEncoder(n_channels, pretrained=pretrained, dropout=dropout)
        self.graph = GraphStage(periods)
        self.heads = HazardHeads()
        self.log_temp = nn.Parameter(torch.zeros(len(H.HEADS)))   # stage 5: 9 scalars
        # output index → head index, for broadcasting temperatures over the 15 outputs
        self.register_buffer("head_of_output", torch.tensor(
            [i for i, h in enumerate(H.HEADS) for _ in h.horizons_h], dtype=torch.long))

    # stage 1
    def normalise(self, x_raw: torch.Tensor, mask: torch.Tensor) -> torch.Tensor:
        return ((x_raw - self.norm_mean) / self.norm_std) * mask

    def set_normalisation(self, mean, std) -> None:
        self.norm_mean.copy_(torch.as_tensor(mean, dtype=torch.float32))
        self.norm_std.copy_(torch.as_tensor(std, dtype=torch.float32).clamp_min(1e-6))

    # stage 2
    def embed(self, x_raw: torch.Tensor, mask: torch.Tensor) -> torch.Tensor:
        return self.encoder(self.normalise(x_raw, mask), mask)

    # stage 4
    def logits(self, emb: torch.Tensor, emb_graph: torch.Tensor | None = None) -> torch.Tensor:
        return self.heads(emb, emb_graph)

    # stage 5
    def temperatures(self) -> torch.Tensor:
        return torch.exp(self.log_temp)

    def calibrated_probs(self, logits: torch.Tensor) -> torch.Tensor:
        T = self.temperatures()[self.head_of_output]
        return torch.sigmoid(logits / T)

    def forward(self, x_raw: torch.Tensor, mask: torch.Tensor) -> dict[str, torch.Tensor]:
        """Standalone path: stages 1, 2, 4, 5 — stage 3 skipped."""
        emb = self.embed(x_raw, mask)
        lg = self.logits(emb)
        return {"emb": emb, "logits": lg, "probs": self.calibrated_probs(lg)}

    def parameter_counts(self) -> dict[str, int]:
        c = lambda m: sum(p.numel() for p in m.parameters())  # noqa: E731
        return {"encoder_backbone": c(self.encoder.backbone), "encoder_proj": c(self.encoder.proj),
                "graph": c(self.graph), "heads": c(self.heads), "temperature": self.log_temp.numel(),
                "total": c(self)}


def quantise_embedding(emb: torch.Tensor) -> torch.Tensor:
    """16-byte int8 mesh payload (§7.5)."""
    return torch.clamp(torch.round(emb * EMB_INT8_SCALE), -127, 127).to(torch.int8)


def dequantise_embedding(q: torch.Tensor) -> torch.Tensor:
    return q.to(torch.float32) / EMB_INT8_SCALE


# ---------------------------------------------------------------- export wrappers
class EncoderExport(nn.Module):
    """ONNX graph 1 — the always-run standalone path (stages 1, 2, 4, 5)."""

    def __init__(self, m: PrahariModel):
        super().__init__()
        self.m = m

    def forward(self, x_raw, mask):
        emb = self.m.embed(x_raw, mask)
        lg = self.m.logits(emb)
        return emb, quantise_embedding(emb), lg, self.m.calibrated_probs(lg)


class GraphExport(nn.Module):
    """ONNX graph 2 — stage 3 + stages 4, 5 on the refined embedding.

    Inputs: own_emb (B,16) this cycle; hist (B, 1+MAX_NEIGHBOURS, 16, P)
    dequantised embedding histories with node 0 = self, oldest → newest;
    adj (B, N, N) adjacency-prior weights; valid (B, N) 1 for nodes that
    reported this round.  Outputs logits/probs for all 15 outputs (the
    five non-graph heads still read own_emb).
    """

    def __init__(self, m: PrahariModel):
        super().__init__()
        self.m = m
        self.dense = DenseGraphStage(m.graph)

    def forward(self, own_emb, hist, adj, valid):
        refined = own_emb + self.dense(hist, adj, valid)
        lg = self.m.logits(own_emb, refined)
        return refined, lg, self.m.calibrated_probs(lg)
