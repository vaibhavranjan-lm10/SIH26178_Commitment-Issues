"""Stage 5 temperature fitting and advisory/warning threshold calibration.

Thresholds are NEVER preset (blueprint §6.8, §12 item 4): each output's
advisory and warning levels come from the precision-recall curve on the
held-out-column validation split.  When a hazard has too few validation
positives for a curve to mean anything, the fallback is explicit and
labelled in the result (``source``) — it is never reported as if it were a
validated threshold (CLAUDE.md pitfall: rare hazards degenerate).

OFFLINE ONLY.
"""
from __future__ import annotations

from dataclasses import asdict, dataclass

import numpy as np
import torch
from sklearn.metrics import average_precision_score, precision_recall_curve

from . import heads as H

MIN_POSITIVES = 10            # below this a PR curve is noise
ADVISORY_RECALL = 0.90        # advisory: lowest threshold that still catches 90 % of events
FIXED_FALLBACK = (0.5, 0.8)   # last resort, clearly labelled

SOURCE_VAL = "val_pr_curve"
SOURCE_TRAIN = "FALLBACK_train_pr_curve"
SOURCE_FIXED = "FALLBACK_fixed_prior"
SOURCE_DECLARED = "not_calibrated_declared_only_head"


@dataclass
class Threshold:
    output: str
    advisory: float
    warning: float
    source: str
    n_val: int
    n_val_pos: int
    n_train_pos: int
    val_ap: float | None
    note: str = ""

    def as_dict(self):
        return asdict(self)


def pr_thresholds(probs: np.ndarray, y: np.ndarray) -> tuple[float, float, float]:
    """(advisory, warning, average precision) from one PR curve.

    warning  = threshold with the best F1 (balanced operating point);
    advisory = lowest threshold at which recall ≥ ADVISORY_RECALL, i.e. the
               earliest point that still catches almost every event.  The
               cost asymmetry (a missed event costs more than a false alarm)
               is why the advisory level leans to recall.
    Ensures advisory ≤ warning.
    """
    prec, rec, thr = precision_recall_curve(y, probs)
    # sklearn appends a final (p=1, r=0) point with no threshold
    prec, rec = prec[:-1], rec[:-1]
    f1 = 2 * prec * rec / np.clip(prec + rec, 1e-9, None)
    warning = float(thr[int(np.argmax(f1))])
    ok = np.nonzero(rec >= ADVISORY_RECALL)[0]
    advisory = float(thr[ok[-1]]) if len(ok) else float(thr[0])   # thresholds ascend; recall descends
    advisory = min(advisory, warning)
    return advisory, warning, float(average_precision_score(y, probs))


def calibrate_thresholds(val_probs: np.ndarray, val_y: np.ndarray,
                         train_probs: np.ndarray, train_y: np.ndarray) -> list[Threshold]:
    out = []
    for k, name in enumerate(H.OUTPUT_NAMES):
        head = H.BY_CODE[name.split("_")[0]]
        nvp, ntp = int(val_y[:, k].sum()), int(train_y[:, k].sum())
        nvn = int((val_y[:, k] == 0).sum())
        if not head.trained:
            out.append(Threshold(name, *FIXED_FALLBACK, SOURCE_DECLARED, len(val_y), nvp, ntp, None,
                                 "declared-only head: no labels, no training, no calibration"))
            continue
        if nvp >= MIN_POSITIVES and nvn >= MIN_POSITIVES:
            a, w, ap = pr_thresholds(val_probs[:, k], val_y[:, k])
            out.append(Threshold(name, a, w, SOURCE_VAL, len(val_y), nvp, ntp, ap))
        elif ntp >= MIN_POSITIVES and int((train_y[:, k] == 0).sum()) >= MIN_POSITIVES:
            a, w, ap = pr_thresholds(train_probs[:, k], train_y[:, k])
            vap = float(average_precision_score(val_y[:, k], val_probs[:, k])) if 0 < nvp < len(val_y) else None
            out.append(Threshold(name, a, w, SOURCE_TRAIN, len(val_y), nvp, ntp, vap,
                                 f"only {nvp} validation positives (< {MIN_POSITIVES}); curve taken on the TRAINING "
                                 "split instead — optimistic, not a validated threshold"))
        else:
            out.append(Threshold(name, *FIXED_FALLBACK, SOURCE_FIXED, len(val_y), nvp, ntp, None,
                                 f"{nvp} val / {ntp} train positives: no usable PR curve anywhere; fixed prior "
                                 "thresholds — NOT calibrated"))
    return out


def fit_temperatures(logits: np.ndarray, y: np.ndarray, grid: np.ndarray | None = None) -> np.ndarray:
    """Per-head temperature minimising validation BCE (NLL) of logits / T.
    Grid search over log T ∈ [−2, 2] (deterministic, convex enough).  Heads
    without both classes in validation keep T = 1.  Declared-only: T = 1."""
    grid = np.linspace(-2.0, 2.0, 401) if grid is None else grid
    lg, yy = torch.from_numpy(logits).float(), torch.from_numpy(y).float()
    T = np.ones(len(H.HEADS), dtype=np.float32)
    sl = H.output_slices()
    for i, h in enumerate(H.HEADS):
        if not h.trained:
            continue
        l_h, y_h = lg[:, sl[h.code]], yy[:, sl[h.code]]
        if y_h.sum() == 0 or (1 - y_h).sum() == 0:
            continue
        best, best_nll = 1.0, float("inf")
        for g in grid:
            t = float(np.exp(g))
            nll = torch.nn.functional.binary_cross_entropy_with_logits(l_h / t, y_h).item()
            if nll < best_nll:
                best, best_nll = t, nll
        T[i] = best
    return T
