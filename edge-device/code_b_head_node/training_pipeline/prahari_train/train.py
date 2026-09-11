"""Training script — OFFLINE, on a dev machine, never on the UNO Q.

    python -m prahari_train.train --out artifacts/v0.1.0-synthetic [--columns 12 --days 120 ...]

Pipeline:
  1. synthetic corpus (PLACEHOLDER — no real data until the five open
     data-pipeline decisions are resolved) → features → 41 trained channels
  2. held-out-COLUMN validation split (see data.split_by_column for why not
     a temporal cut)
  3. phase 1: standalone path (TTM encoder + heads), BCE over the seven
     trained heads' outputs; GL/WQ excluded from the loss
  4. phase 2: encoder frozen, embeddings cached (the deployment-time cache),
     A3TGCN graph stage + the four graph heads trained on ego-network
     samples, with random neighbour drop-out and a share of graph-off
     samples so the heads keep working when stage 3 is skipped
  5. per-head temperature scaling on validation logits
  6. advisory/warning thresholds from the validation PR curve, labelled
     fallbacks where positives are too few
  7. ONNX export (torchscript exporter pinned), loaded and run in
     onnxruntime, INT8 copies, manifest with weight hash

Train AND validation losses are reported for every epoch as they are.
"""
from __future__ import annotations

import argparse
import copy
import json
import time
from pathlib import Path

import numpy as np
import torch
import torch.nn.functional as Fnn
from sklearn.metrics import average_precision_score
from torch.utils.data import DataLoader

from . import calibrate as C
from . import data as D
from . import export as E
from . import heads as H
from . import model as M
from . import registry
from . import synthetic as S


def parse_args(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--version", default="v0.1.0-synthetic")
    p.add_argument("--columns", type=int, default=12)
    p.add_argument("--days", type=int, default=120)
    p.add_argument("--seed", type=int, default=0)
    p.add_argument("--epochs", type=int, default=6, help="phase 1 epochs")
    p.add_argument("--graph-epochs", type=int, default=8, help="phase 2 epochs")
    p.add_argument("--stride", type=int, default=4, help="training window stride (hours)")
    p.add_argument("--val-stride", type=int, default=2)
    p.add_argument("--batch", type=int, default=32)
    p.add_argument("--lr", type=float, default=1e-3)
    p.add_argument("--lr-backbone", type=float, default=1e-4)
    p.add_argument("--val-frac", type=float, default=0.25)
    p.add_argument("--no-pretrained", action="store_true", help="random-init TTM (smoke tests only)")
    p.add_argument("--threads", type=int, default=8)
    p.add_argument("--max-train-batches", type=int, default=0, help="debug: cap batches per epoch")
    return p.parse_args(argv)


# ---------------------------------------------------------------- losses / metrics
def trained_output_mask() -> torch.Tensor:
    return torch.tensor([H.BY_CODE[n.split("_")[0]].trained for n in H.OUTPUT_NAMES], dtype=torch.float32)


def pos_weights(columns, ids) -> torch.Tensor:
    y = np.concatenate([columns[i].labels for i in ids], 0)
    pos, neg = y.sum(0), (1 - y).sum(0)
    w = np.where(pos > 0, neg / np.clip(pos, 1, None), 1.0)
    return torch.tensor(np.clip(w, 1.0, 30.0), dtype=torch.float32)


def masked_bce(logits, y, pw, omask):
    l = Fnn.binary_cross_entropy_with_logits(logits, y, pos_weight=pw, reduction="none")
    return (l * omask).sum() / (omask.sum() * len(y))


def ap_table(probs: np.ndarray, y: np.ndarray) -> dict[str, float | None]:
    out = {}
    for k, n in enumerate(H.OUTPUT_NAMES):
        out[n] = float(average_precision_score(y[:, k], probs[:, k])) if 0 < y[:, k].sum() < len(y) else None
    return out


# ---------------------------------------------------------------- phase 1
@torch.no_grad()
def evaluate_standalone(model, loader, pw, omask):
    model.eval()
    tot, n, lgs, ys, embs, keys = 0.0, 0, [], [], [], []
    for x, m, y, c, t in loader:
        out = model(x, m)
        tot += masked_bce(out["logits"], y, pw, omask).item() * len(y); n += len(y)
        lgs.append(out["logits"].numpy()); ys.append(y.numpy()); embs.append(out["emb"].numpy())
        keys.extend(zip(c.tolist(), t.tolist()))
    return tot / max(n, 1), np.concatenate(lgs), np.concatenate(ys), np.concatenate(embs), keys


def train_standalone(model, columns, train_ids, val_ids, args, log):
    omask = trained_output_mask()
    pw = pos_weights(columns, train_ids)
    tr = DataLoader(D.WindowDataset(columns, train_ids, stride=args.stride), batch_size=args.batch, shuffle=True)
    va = DataLoader(D.WindowDataset(columns, val_ids, stride=args.val_stride), batch_size=args.batch * 2)
    bb = list(model.encoder.backbone.parameters())
    rest = [p for n, p in model.named_parameters() if not n.startswith("encoder.backbone") and not n.startswith("graph") and n != "log_temp"]
    opt = torch.optim.AdamW([{"params": bb, "lr": args.lr_backbone}, {"params": rest, "lr": args.lr}], weight_decay=1e-4)
    history, best, best_state = [], float("inf"), None
    for ep in range(1, args.epochs + 1):
        model.train(); t0 = time.time(); tot, n = 0.0, 0
        for b, (x, m, y, c, t) in enumerate(tr):
            if args.max_train_batches and b >= args.max_train_batches:
                break
            out = model(x, m)
            loss = masked_bce(out["logits"], y, pw, omask)
            opt.zero_grad(); loss.backward()
            torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
            opt.step()
            tot += loss.item() * len(y); n += len(y)
        vloss, vlg, vy, _, _ = evaluate_standalone(model, va, pw, omask)
        with torch.no_grad():
            vprob = torch.sigmoid(torch.from_numpy(vlg)).numpy()
        rec = {"epoch": ep, "train_loss": tot / max(n, 1), "val_loss": vloss, "seconds": round(time.time() - t0, 1),
               "val_ap": ap_table(vprob, vy)}
        history.append(rec)
        log(f"[phase1] epoch {ep}/{args.epochs}  train_loss={rec['train_loss']:.4f}  val_loss={vloss:.4f}  ({rec['seconds']}s)")
        if vloss < best:
            best, best_state = vloss, copy.deepcopy(model.state_dict())
    if best_state is not None:
        model.load_state_dict(best_state)
    return history, pw, omask


# ---------------------------------------------------------------- phase 2
def graph_collate(batch):
    own, hist, adj, valid, y, c, t = zip(*batch)
    return (torch.stack(own), torch.stack(hist), torch.stack(adj), torch.stack(valid), torch.stack(y),
            torch.tensor(c), torch.tensor(t))


def sparse_from_padded(hist, adj, valid):
    samples = []
    for b in range(hist.shape[0]):
        n = int(valid[b].sum().item())
        samples.append((hist[b, :n], adj[b, :n, :n], None))
    return M.build_batch_graph(samples)


def graph_forward(model, own, hist, adj, valid, graph_on: torch.Tensor):
    """Logits with stage 3 applied to the samples flagged graph_on, skipped for the rest."""
    X, ei, ew, roots = sparse_from_padded(hist, adj, valid)
    refined = own + model.graph(X, ei, ew)[roots]
    lg_on = model.logits(own, refined)
    lg_off = model.logits(own)
    return torch.where(graph_on[:, None], lg_on, lg_off), refined


@torch.no_grad()
def evaluate_graph(model, loader, pw, omask):
    model.eval()
    res = {}
    for mode in ("on", "off"):
        tot, n, lgs, ys = 0.0, 0, [], []
        for own, hist, adj, valid, y, c, t in loader:
            flag = torch.full((len(y),), mode == "on")
            lg, _ = graph_forward(model, own, hist, adj, valid, flag)
            tot += masked_bce(lg, y, pw, omask).item() * len(y); n += len(y)
            lgs.append(lg.numpy()); ys.append(y.numpy())
        lg, yy = np.concatenate(lgs), np.concatenate(ys)
        res[mode] = {"loss": tot / max(n, 1), "ap": ap_table(torch.sigmoid(torch.from_numpy(lg)).numpy(), yy)}
    return res


def train_graph(model, emb_cache, columns, train_ids, val_ids, ego, pw, omask, args, log, graph_off_frac=0.3):
    for p in model.encoder.parameters():
        p.requires_grad_(False)
    gtr = DataLoader(D.GraphSampleDataset(emb_cache, columns, train_ids, ego, stride=max(1, args.stride // 2), seed=args.seed),
                     batch_size=args.batch * 2, shuffle=True, collate_fn=graph_collate)
    gva = DataLoader(D.GraphSampleDataset(emb_cache, columns, val_ids, ego, stride=args.val_stride, nbr_dropout=0.0, seed=1),
                     batch_size=args.batch * 4, collate_fn=graph_collate)
    head_params = [p for h in H.HEADS if h.graph for p in model.heads.mlps[h.code].parameters()]
    opt = torch.optim.AdamW([{"params": model.graph.parameters(), "lr": args.lr},
                             {"params": head_params, "lr": args.lr * 0.1}], weight_decay=1e-4)
    gmask = omask * torch.tensor([H.BY_CODE[n.split("_")[0]].graph for n in H.OUTPUT_NAMES], dtype=torch.float32)
    rng = np.random.default_rng(args.seed)
    history = []
    before = evaluate_graph(model, gva, pw, gmask)
    log(f"[phase2] before: val_loss(graph heads) on={before['on']['loss']:.4f} off={before['off']['loss']:.4f}")
    for ep in range(1, args.graph_epochs + 1):
        model.train(); model.encoder.eval(); t0 = time.time(); tot, n = 0.0, 0
        for own, hist, adj, valid, y, c, t in gtr:
            flag = torch.from_numpy(rng.random(len(y)) >= graph_off_frac)
            lg, _ = graph_forward(model, own, hist, adj, valid, flag)
            loss = masked_bce(lg, y, pw, gmask)
            opt.zero_grad(); loss.backward(); opt.step()
            tot += loss.item() * len(y); n += len(y)
        ev = evaluate_graph(model, gva, pw, gmask)
        rec = {"epoch": ep, "train_loss": tot / max(n, 1), "val_loss_graph_on": ev["on"]["loss"],
               "val_loss_graph_off": ev["off"]["loss"], "val_ap_graph_on": ev["on"]["ap"], "val_ap_graph_off": ev["off"]["ap"],
               "seconds": round(time.time() - t0, 1)}
        history.append(rec)
        log(f"[phase2] epoch {ep}/{args.graph_epochs}  train_loss={rec['train_loss']:.4f}  "
            f"val_loss on={rec['val_loss_graph_on']:.4f} off={rec['val_loss_graph_off']:.4f}  ({rec['seconds']}s)")
    return {"before": {k: v["loss"] for k, v in before.items()}, "epochs": history}, gva


# ---------------------------------------------------------------- main
def run(args) -> dict:
    torch.manual_seed(args.seed); np.random.seed(args.seed)
    torch.set_num_threads(args.threads)
    args.out.mkdir(parents=True, exist_ok=True)
    lines = []

    def log(s):
        print(s, flush=True); lines.append(s)

    registry.verify_counts()
    t_all = time.time()
    log(f"== corpus: SYNTHETIC PLACEHOLDER, {args.columns} columns × {args.days} days, seed {args.seed}")
    corpus = S.generate_corpus(args.columns, args.days, seed=args.seed)
    summary = S.corpus_summary(corpus)
    log(summary.to_string())
    columns = D.prepare_columns(corpus)
    train_ids, val_ids = D.split_by_column(columns, args.val_frac, args.seed)
    log(f"== split by column: train={[columns[i].cid for i in train_ids]} val={[columns[i].cid for i in val_ids]}")
    label_counts = {"train": {n: int(sum(columns[i].labels[:, k].sum() for i in train_ids)) for k, n in enumerate(H.OUTPUT_NAMES)},
                    "val": {n: int(sum(columns[i].labels[:, k].sum() for i in val_ids)) for k, n in enumerate(H.OUTPUT_NAMES)}}
    log("== positive labels (hours) train: " + json.dumps(label_counts["train"]))
    log("== positive labels (hours) val:   " + json.dumps(label_counts["val"]))

    model = M.PrahariModel(pretrained=not args.no_pretrained)
    mean, std = D.normalisation_stats([columns[i] for i in train_ids])
    model.set_normalisation(mean, std)
    log(f"== model: {model.parameter_counts()}  encoder={model.encoder.pretrained_source}")

    log("== phase 1: standalone path (encoder + heads)")
    hist1, pw, omask = train_standalone(model, columns, train_ids, val_ids, args, log)

    log("== phase 2: embedding cache + graph stage")
    t0 = time.time()
    emb_cache = D.embed_all(model, columns, batch=args.batch * 2)
    log(f"   cached {emb_cache.shape} embeddings in {time.time() - t0:.0f}s")
    ego = D.ego_networks(corpus)
    hist2, gva = train_graph(model, emb_cache, columns, train_ids, val_ids, ego, pw, omask, args, log)

    log("== temperature scaling (validation logits, standalone path)")
    va = DataLoader(D.WindowDataset(columns, val_ids, stride=args.val_stride), batch_size=args.batch * 2)
    tr_eval = DataLoader(D.WindowDataset(columns, train_ids, stride=args.stride), batch_size=args.batch * 2)
    _, vlg, vy, _, _ = evaluate_standalone(model, va, pw, omask)
    _, tlg, ty, _, _ = evaluate_standalone(model, tr_eval, pw, omask)
    T = C.fit_temperatures(vlg, vy)
    with torch.no_grad():
        model.log_temp.copy_(torch.log(torch.from_numpy(T)))
    log("   temperatures: " + json.dumps({h.code: round(float(t), 3) for h, t in zip(H.HEADS, T)}))
    with torch.no_grad():
        vprob = model.calibrated_probs(torch.from_numpy(vlg)).numpy()
        tprob = model.calibrated_probs(torch.from_numpy(tlg)).numpy()

    log("== thresholds (PR curve on held-out columns; fallbacks labelled)")
    thr = C.calibrate_thresholds(vprob, vy, tprob, ty)
    for t in thr:
        log(f"   {t.output:8s} advisory={t.advisory:.3f} warning={t.warning:.3f}  source={t.source:28s} "
            f"val_pos={t.n_val_pos:5d} train_pos={t.n_train_pos:5d} val_AP={'-' if t.val_ap is None else f'{t.val_ap:.3f}'}  {t.note}")

    report = {
        "corpus": {"kind": "SYNTHETIC PLACEHOLDER", "columns": args.columns, "days": args.days, "seed": args.seed,
                   "cadence_hours": 1.0, "summary": summary.to_dict(orient="records")},
        "split": {"rule": "held-out columns, profile-stratified", "train": [columns[i].cid for i in train_ids],
                  "val": [columns[i].cid for i in val_ids]},
        "label_positives_hours": label_counts,
        "phase1_standalone": hist1,
        "phase2_graph": hist2,
        "temperatures": {h.code: float(t) for h, t in zip(H.HEADS, T)},
        "val_ap_calibrated_standalone": ap_table(vprob, vy),
        "args": {k: (str(v) if isinstance(v, Path) else v) for k, v in vars(args).items()},
        "wallclock_s": round(time.time() - t_all, 1),
    }

    log("== export")
    x, m, _, _, _ = next(iter(va))
    own, hst, adj, valid, _, _, _ = next(iter(gva))
    manifest = E.export_and_verify(model, args.out, x[:4], m[:4], (own[:4], hst[:4], adj[:4], valid[:4]),
                                   [t.as_dict() for t in thr], report, args.version)
    log("   verification: " + json.dumps(manifest["verification"]))
    log(f"   weight hash: {manifest['weight_hash_sha256']}")
    torch.save(model.state_dict(), args.out / "model.pt")
    (args.out / "training_report.json").write_text(json.dumps(report, indent=2))
    (args.out / "training_log.txt").write_text("\n".join(lines))
    log(f"== done in {report['wallclock_s']}s → {args.out}")
    return manifest


def main(argv=None):
    run(parse_args(argv))


if __name__ == "__main__":
    main()
