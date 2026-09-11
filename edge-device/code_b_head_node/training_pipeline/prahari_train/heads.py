"""The nine hazard heads — blueprint §9, transcribed.

Horizons are in hours; ``0`` is t+0 detection.  ``graph`` marks the four
spatially-propagating hazards that use the optional graph stage (§9: "Only
the four spatially-propagating hazards use the graph stage" — FL, UF, FI,
PO).  ``trained`` is False for the two declared-only heads (GL, WQ), which
are present in the architecture and the ONNX output but excluded from the
loss and never calibrated.
"""
from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class Head:
    code: str
    name: str
    horizons_h: tuple[int, ...]
    graph: bool
    trained: bool

    @property
    def n_outputs(self) -> int:
        return len(self.horizons_h)

    def output_names(self) -> list[str]:
        return [f"{self.code}_t{h}" for h in self.horizons_h]


HEADS: tuple[Head, ...] = (
    Head("FL", "riverine flood", (0, 24), graph=True, trained=True),
    Head("UF", "urban / flash flood", (0, 6), graph=True, trained=True),
    Head("FI", "forest fire", (0, 24), graph=True, trained=True),
    Head("PO", "air pollution", (0, 24), graph=True, trained=True),
    Head("LS", "landslide", (0, 24), graph=False, trained=True),
    Head("HW", "extreme heat", (72,), graph=False, trained=True),
    Head("CY", "cyclone proximity", (48,), graph=False, trained=True),
    Head("GL", "industrial gas leak", (0,), graph=False, trained=False),
    Head("WQ", "water quality degradation", (0, 24), graph=False, trained=False),
)
BY_CODE = {h.code: h for h in HEADS}
GRAPH_HEADS = tuple(h.code for h in HEADS if h.graph)
TRAINED_HEADS = tuple(h.code for h in HEADS if h.trained)
DECLARED_ONLY_HEADS = tuple(h.code for h in HEADS if not h.trained)
OUTPUT_NAMES = [n for h in HEADS for n in h.output_names()]   # 15 logits, fixed order
N_OUTPUTS = len(OUTPUT_NAMES)


def output_slices() -> dict[str, slice]:
    """{head code: slice into the 15-wide output vector}."""
    out, i = {}, 0
    for h in HEADS:
        out[h.code] = slice(i, i + h.n_outputs)
        i += h.n_outputs
    return out


assert len(HEADS) == 9 and GRAPH_HEADS == ("FL", "UF", "FI", "PO") and N_OUTPUTS == 15
