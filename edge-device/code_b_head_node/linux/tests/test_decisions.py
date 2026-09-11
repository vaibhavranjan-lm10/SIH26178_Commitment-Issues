"""Alert decisioning (hysteresis, CAP, provenance) and the duty-cycle state machine."""
import xml.etree.ElementTree as ET

from prahari_hn import alerts as A
from prahari_hn.duty_cycle import BASELINE, ELEVATED, DutyCycle
from prahari_hn.model_registry import ModelBundle, Threshold

HEADS = [{"code": "FL", "name": "riverine flood", "horizons_h": [0, 24], "graph": True, "trained": True},
         {"code": "HW", "name": "extreme heat", "horizons_h": [72], "graph": False, "trained": True},
         {"code": "GL", "name": "gas leak", "horizons_h": [0], "graph": False, "trained": False}]
OUTPUTS = ["FL_t0", "FL_t24", "HW_t72", "GL_t0"]


def bundle():
    thr = {"FL_t0": Threshold("FL_t0", 0.3, 0.7, "val_pr_curve"), "FL_t24": Threshold("FL_t24", 0.4, 0.8, "val_pr_curve"),
           "HW_t72": Threshold("HW_t72", 0.5, 0.9, "FALLBACK_train_pr_curve"), "GL_t0": Threshold("GL_t0", 0.5, 0.8, "not_calibrated_declared_only_head")}
    return ModelBundle("v", None, "abc123", [], [], [], OUTPUTS, HEADS, {}, thr, 512, 41, 16, 16.0, 3, 8, None, None, "", {})


def test_two_tier_with_hysteresis_and_cap():
    e = A.AlertEngine(bundle(), "node-1", clear_cycles=2, site_name="village")
    p = {"FL_t0": 0.1, "FL_t24": 0.1, "HW_t72": 0.1, "GL_t0": 0.99}
    assert e.decide(p, 1000) == []                             # GL never alerts
    out = e.decide({**p, "FL_t24": 0.45}, 1001)
    assert len(out) == 1 and out[0].level == A.LEVEL_ADVISORY and out[0].output == "FL_t24" and out[0].escalation
    out = e.decide({**p, "FL_t0": 0.75, "FL_t24": 0.45}, 1002)
    assert out[0].level == A.LEVEL_WARNING and out[0].output == "FL_t0" and out[0].horizon_h == 0
    cap = out[0].cap
    assert cap["msgType"] == "Alert" and cap["info"]["severity"] == "Severe" and cap["info"]["urgency"] == "Immediate"
    params = {q["valueName"]: q["value"] for q in cap["info"]["parameter"]}
    assert params["prahari:threshold_source"] == "val_pr_curve" and params["prahari:model_weight_hash"] == "abc123"
    xml = A.cap_xml(cap)
    root = ET.fromstring(xml)
    assert root.tag.endswith("alert") and "Riverine flood warning" in xml
    # hysteresis: one cycle below does not downgrade, two do
    assert e.decide(p, 1003) == [] and e.any_warning()
    out = e.decide(p, 1004)
    assert out[0].level == A.LEVEL_NONE and out[0].cap["msgType"] == "Cancel" and not e.any_warning()
    # fallback-calibrated head carries its provenance
    out = e.decide({**p, "HW_t72": 0.95}, 1005)
    assert out[0].threshold_source.startswith("FALLBACK") and out[0].cap["info"]["urgency"] == "Future"
    st = e.export_state(); e2 = A.AlertEngine(bundle(), "node-1"); e2.import_state(st)
    assert e2.levels() == e.levels() == {"FL": 0, "HW": 2, "GL": 0}


def test_duty_cycle_state_machine():
    d = DutyCycle(baseline_s=3600, elevated_s=300, cooldown_cycles=3)
    assert d.cadence_s == 3600 and d.update(False) is False
    assert d.update(True) is True and d.mode == ELEVATED and d.cadence_s == 300
    assert d.update(False) is False and d.update(False) is False           # cooling
    assert d.update(True) is False and d.calm_streak == 0                  # warning again resets the cooldown
    assert [d.update(False) for _ in range(3)] == [False, False, True] and d.mode == BASELINE
    d2 = DutyCycle(); d2.import_state({"mode": "elevated", "calm_streak": 1})
    assert d2.mode == ELEVATED and d2.cadence_s == 300
