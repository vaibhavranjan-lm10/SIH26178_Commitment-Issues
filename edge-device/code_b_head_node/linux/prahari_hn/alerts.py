"""Alert decisioning (§6.8): two-tier advisory/warning per head against the
calibrated thresholds from the model manifest, CAP-formatted, siren command
to the MCU, queued locally when the uplink is down.

Per head the level is the highest tier any of its horizons crosses.  A
level is entered immediately; it is left only after ``clear_cycles``
consecutive cycles below that tier's threshold (hysteresis, so a
probability hovering at a threshold does not toggle the siren).  Declared-
only heads (GL, WQ) are never alerted: their thresholds are not calibrated.
Every alert records the threshold's provenance so a fallback-calibrated
threshold is visible downstream.
"""
from __future__ import annotations

import time
import uuid
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field

from .model_registry import ModelBundle

LEVEL_NONE, LEVEL_ADVISORY, LEVEL_WARNING = 0, 1, 2
LEVEL_NAME = {0: "clear", 1: "advisory", 2: "warning"}
SEVERITY = {1: "Moderate", 2: "Severe"}
CAP_EVENT = {"FL": "Riverine flood", "UF": "Flash flood", "FI": "Forest fire", "PO": "Air pollution",
             "LS": "Landslide", "HW": "Extreme heat", "CY": "Cyclone", "GL": "Gas leak", "WQ": "Water quality"}
CAP_CATEGORY = {"FL": "Met", "UF": "Met", "FI": "Fire", "PO": "Env", "LS": "Geo", "HW": "Met", "CY": "Met",
                "GL": "CBRNE", "WQ": "Env"}


@dataclass
class HeadState:
    level: int = 0
    below_streak: int = 0


@dataclass
class Alert:
    head: str
    level: int
    prev_level: int
    output: str            # the horizon output that drove the decision
    probability: float
    horizon_h: int
    threshold: float
    threshold_source: str
    ts: int
    cap: dict = field(default_factory=dict)

    @property
    def escalation(self) -> bool:
        return self.level > self.prev_level


class AlertEngine:
    def __init__(self, bundle: ModelBundle, node_id: str, clear_cycles: int = 3, site_name: str = ""):
        self.b, self.node_id, self.clear_cycles, self.site_name = bundle, node_id, clear_cycles, site_name
        self.state: dict[str, HeadState] = {h["code"]: HeadState() for h in bundle.heads}

    # persistence
    def export_state(self) -> dict:
        return {k: [v.level, v.below_streak] for k, v in self.state.items()}

    def import_state(self, d: dict | None) -> None:
        for k, (lvl, streak) in (d or {}).items():
            if k in self.state:
                self.state[k] = HeadState(int(lvl), int(streak))

    def levels(self) -> dict[str, int]:
        return {k: v.level for k, v in self.state.items()}

    def any_warning(self) -> bool:
        return any(v.level == LEVEL_WARNING for v in self.state.values())

    def decide(self, probs: dict[str, float], ts: int | None = None) -> list[Alert]:
        ts = int(ts if ts is not None else time.time())
        out: list[Alert] = []
        for h in self.b.heads:
            if not h["trained"]:
                continue
            code = h["code"]
            best_level, best = LEVEL_NONE, None
            for hz in h["horizons_h"]:
                name = f"{code}_t{hz}"
                p, thr = float(probs[name]), self.b.thresholds[name]
                lvl = LEVEL_WARNING if p >= thr.warning else LEVEL_ADVISORY if p >= thr.advisory else LEVEL_NONE
                if lvl > best_level or best is None or (lvl == best_level and p > best[0]):
                    best_level, best = lvl, (p, name, hz, thr)
            st = self.state[code]
            new = st.level
            if best_level > st.level:
                new, st.below_streak = best_level, 0
            elif best_level < st.level:
                st.below_streak += 1
                if st.below_streak >= self.clear_cycles:
                    new, st.below_streak = best_level, 0
            else:
                st.below_streak = 0
            if new != st.level:
                p, name, hz, thr = best
                thr_val = thr.warning if new == LEVEL_WARNING else thr.advisory
                a = Alert(code, new, st.level, name, p, hz, thr_val, thr.source, ts)
                a.cap = self.cap_message(a)
                out.append(a)
                st.level = new
        return out

    def cap_message(self, a: Alert) -> dict:
        """CAP 1.2 fields as a dict (see cap_xml for the XML serialisation)."""
        code = a.head
        urgency = "Immediate" if a.horizon_h == 0 else "Expected" if a.horizon_h <= 6 else "Future"
        certainty = "Likely" if a.probability >= 0.8 else "Possible"
        sent = time.strftime("%Y-%m-%dT%H:%M:%S+00:00", time.gmtime(a.ts))
        expires = time.strftime("%Y-%m-%dT%H:%M:%S+00:00", time.gmtime(a.ts + max(a.horizon_h, 1) * 3600))
        return {
            "identifier": f"{self.node_id}-{a.ts}-{code}-{uuid.uuid4().hex[:8]}",
            "sender": self.node_id, "sent": sent,
            "status": "Actual", "msgType": "Alert" if a.level > LEVEL_NONE else "Cancel",
            "scope": "Public",
            "info": {
                "category": CAP_CATEGORY[code], "event": CAP_EVENT[code],
                "urgency": urgency,
                "severity": SEVERITY.get(a.level, "Minor"),
                "certainty": certainty,
                "effective": sent, "expires": expires,
                "headline": f"{CAP_EVENT[code]} {LEVEL_NAME[a.level]} — {self.site_name or self.node_id}",
                "description": (f"{CAP_EVENT[code]} probability {a.probability:.2f} at t+{a.horizon_h} h "
                                f"({'crossed' if a.level > a.prev_level else 'fell below'} the "
                                f"{LEVEL_NAME[max(a.level, a.prev_level)]} threshold {a.threshold:.2f})."),
                "parameter": [
                    {"valueName": "prahari:head", "value": code},
                    {"valueName": "prahari:output", "value": a.output},
                    {"valueName": "prahari:probability", "value": f"{a.probability:.4f}"},
                    {"valueName": "prahari:confidence", "value": f"{a.probability:.4f}"},
                    {"valueName": "prahari:level", "value": str(a.level)},
                    {"valueName": "prahari:threshold", "value": f"{a.threshold:.4f}"},
                    {"valueName": "prahari:threshold_source", "value": a.threshold_source},
                    {"valueName": "prahari:model_weight_hash", "value": self.b.weight_hash},
                ],
            },
        }


def cap_xml(cap: dict) -> str:
    ns = "urn:oasis:names:tc:emergency:cap:1.2"
    root = ET.Element("alert", xmlns=ns)
    for k in ("identifier", "sender", "sent", "status", "msgType", "scope"):
        ET.SubElement(root, k).text = str(cap[k])
    info = ET.SubElement(root, "info")
    for k in ("category", "event", "urgency", "severity", "certainty", "effective", "expires", "headline", "description"):
        ET.SubElement(info, k).text = str(cap["info"][k])
    for p in cap["info"]["parameter"]:
        pe = ET.SubElement(info, "parameter")
        ET.SubElement(pe, "valueName").text = p["valueName"]
        ET.SubElement(pe, "value").text = p["value"]
    return ET.tostring(root, encoding="unicode")
