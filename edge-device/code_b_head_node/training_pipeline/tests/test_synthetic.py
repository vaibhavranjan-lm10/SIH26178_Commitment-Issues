"""Synthetic corpus: shape, masking, labels, propagation, determinism."""
import numpy as np
import pytest

from prahari_train import features as F
from prahari_train import heads as H
from prahari_train import registry
from prahari_train import synthetic as S


@pytest.fixture(scope="module")
def corpus():
    return S.generate_corpus(n_columns=8, n_days=70, seed=3, rates={"CY": 2.0, "FL": 4.0, "HW": 8.0})


def test_it_says_it_is_a_placeholder():
    assert "PLACEHOLDER" in S.__doc__ and "NOT" in S.__doc__
    assert "PLACEHOLDER" in S.generate_corpus.__doc__


def test_shapes_channels_and_index(corpus):
    assert len(corpus.columns) == 8
    for col in corpus.columns:
        assert col.primaries.index.equals(corpus.index) and col.labels.index.equals(corpus.index)
        assert list(col.labels.columns) == H.OUTPUT_NAMES
        for c in col.primaries.columns:
            assert c in registry.BY_ID and registry.get(c).tier == "primary"
        # every satellite channel present; in-situ per profile
        assert all(f"P{i}" in col.primaries.columns for i in range(47, 70))
        for c in S.PROFILE_CHANNELS[col.site.profile]:
            assert c in col.primaries.columns
        assert "P19" not in col.primaries.columns or "P18" in col.primaries.columns
        for k in registry.trained_channels():
            if k.startswith("P7") or k.startswith("P8") or k in ("S30", "S33"):
                assert k in col.site.static
    assert F.cadence_hours(corpus.index) == 1.0


def test_dropouts_are_masked_not_filled(corpus):
    col = corpus.columns[0]
    assert col.primaries.isna().any().any(), "expected some pod drop-outs"
    # a drop-out hits a whole pod group at once
    na = col.primaries[["P13", "P15", "P17"]].isna()
    assert (na.sum(axis=1).isin([0, 3])).all()


def test_labels_are_consistent_with_events(corpus):
    for col in corpus.columns:
        for e in col.events:
            assert col.labels[f"{e.hazard}_t{H.BY_CODE[e.hazard].horizons_h[0]}"].iloc[e.start: e.end].all() or H.BY_CODE[e.hazard].horizons_h[0] != 0
        for h in H.HEADS:
            if not h.trained:
                assert (col.labels[[n for n in h.output_names()]] == 0).all().all()
            for hz in h.horizons_h:
                if hz == 0:
                    continue
                y0 = col.labels[f"{h.code}_t0"] if f"{h.code}_t0" in col.labels else None
                yh = col.labels[f"{h.code}_t{hz}"].to_numpy()
                if y0 is not None:
                    a = y0.to_numpy()
                    # forecast label at t is 1 iff active somewhere in (t, t+hz]
                    for t in (100, 500, 900):
                        assert yh[t] == float(a[t + 1: t + hz + 1].any())


def test_events_leave_signatures_in_primaries(corpus):
    seen = set()
    for col in corpus.columns:
        p = col.primaries
        for e in col.events:
            seen.add(e.hazard)
            w = slice(e.start, e.end)
            if e.hazard == "FI" and "P33" in p:
                assert p["P33"].iloc[w].mean() > 3 * p["P33"].iloc[: e.start - 200].mean()
            if e.hazard == "HW":
                assert p["P13"].iloc[w].mean() > p["P13"].median() + 2.5
            if e.hazard == "CY":
                assert p["P17"].iloc[e.start - 48: e.end].min() < p["P17"].median() - 10
            if e.hazard == "FL" and "P11" in p:
                assert p["P11"].iloc[w].max() > 1.8
    assert {"FL", "HW"} <= seen


def test_spatial_propagation_over_graph(corpus):
    assert corpus.edges, "graph must have edges"
    A = corpus.adjacency()
    assert (A == A.T).all() and (np.diag(A) == 0).all()
    # a flood at column k with a lower-elevation neighbour appears at that neighbour later
    found = False
    for i, col in enumerate(corpus.columns):
        for e in col.events:
            if e.hazard != "FL":
                continue
            for j in np.nonzero(A[i])[0]:
                later = [f for f in corpus.columns[j].events if f.hazard == "FL" and 0 < f.start - e.start < 12]
                if later:
                    found = True
    assert found


def test_deterministic_for_seed():
    a = S.generate_corpus(n_columns=3, n_days=40, seed=7)
    b = S.generate_corpus(n_columns=3, n_days=40, seed=7)
    for x, y in zip(a.columns, b.columns):
        assert x.primaries.equals(y.primaries) and x.labels.equals(y.labels)


def test_features_run_on_generated_columns(corpus):
    col = corpus.columns[1]
    feats = F.compute_features(col.primaries, col.site.static)
    vals, mask = F.assemble_trained_channels(col.primaries, feats, col.site.static)
    assert vals.shape == (len(corpus.index), 41)
    assert mask.iloc[40 * 24:].mean().mean() > 0.8   # after warm-up most channels observed
    assert np.isfinite(vals.to_numpy()).all()
