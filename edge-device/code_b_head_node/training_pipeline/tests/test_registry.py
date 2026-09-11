"""Registry must match docs/reference/prahari_parameters.md exactly."""
import pytest

from prahari_train import registry as r


def test_counts_match_spec():
    c = r.verify_counts()
    assert c["total"] == 146
    assert c["by_tier"] == {"primary": 82, "secondary": 39, "tertiary": 17, "operational": 8}
    assert c["trained_total"] == 41
    assert c["trained_split"] == {"in_situ": 16, "satellite": 11, "secondary": 12, "tertiary": 2}


def test_ids_are_contiguous_and_unique():
    ids = [p.id for p in r.PARAMETERS]
    assert ids == [f"P{i}" for i in range(1, 83)] + [f"S{i}" for i in range(1, 40)] \
        + [f"T{i}" for i in range(1, 18)] + [f"O{i}" for i in range(1, 9)]


def test_star_subset_is_exactly_the_documented_one():
    starred = {p.id for p in r.trained()}
    assert starred == {
        "P1", "P2", "P3", "P10", "P11", "P12", "P13", "P15", "P17", "P18", "P19", "P20",
        "P22", "P23", "P32", "P33",
        "P47", "P49", "P51", "P52", "P54", "P57", "P58", "P60", "P70", "P75", "P77",
        "S1", "S2", "S4", "S6", "S9", "S13", "S15", "S16", "S18", "S30", "S33", "S35",
        "T6", "T11",
    }


def test_trained_channels_reconcile_to_41():
    ch = r.trained_channels()
    assert len(ch) == 41
    assert "P19" not in ch, "raw bearing must never be a model input (spec rule)"
    assert "S15" in ch and "S16" in ch
    assert ch.count("P77_lat") == 1 and ch.count("P77_lon") == 1
    assert len(set(ch)) == len(ch)


def test_dependencies_resolve_and_are_acyclic():
    order = r.topological_order()
    pos = {i: k for k, i in enumerate(order)}
    for pid in order:
        for d in r.get(pid).inputs:
            assert d in r.BY_ID
            if r.get(d).inputs:  # secondary/tertiary input must come first
                assert pos[d] < pos[pid], (pid, d)
    assert len(order) == 39 + 17
    assert r.dependencies("T6", transitive=True)[:2] == ["T4", "T5"]
    assert "P12" in r.dependencies("T6", transitive=True)


def test_operational_have_no_hazards():
    for p in r.by_tier("operational"):
        assert p.hazards == ()


def test_by_hazard_lookup():
    fl = r.by_hazard("FL")
    assert r.get("P11") in fl and r.get("O1") not in fl
    assert all(p.trained for p in r.by_hazard("CY", trained_only=True))
    with pytest.raises(KeyError):
        r.by_hazard("XX")


def test_per_hazard_table_conflict_is_flagged_not_hidden():
    """The spec's 'Parameters per hazard' table does not reconcile with its own
    per-row hazard tags.  We must surface that, never quietly match one side."""
    d = r.per_hazard_discrepancies()
    assert d, "if this becomes empty the spec was corrected — remove this test"
    with pytest.raises(AssertionError):
        r.verify_counts(strict_per_hazard=True)


def test_sin_cos_rule_encoded():
    assert r.get("S31").name.endswith("sin component")
    assert r.get("S32").name.endswith("cos component")
    assert r.get("S15").inputs == ("P18", "P19")
