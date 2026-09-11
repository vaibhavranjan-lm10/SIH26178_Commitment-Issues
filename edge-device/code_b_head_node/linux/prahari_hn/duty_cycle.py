"""Risk-adaptive duty cycling (§6.4): hourly at baseline, every 5 minutes
once any head crosses a warning threshold.  Power follows risk.

    BASELINE --any warning--> ELEVATED
    ELEVATED --cooldown_cycles consecutive cycles with no warning--> BASELINE

The MCU side owns the RS-485 sweep / LoRa timing; it is told the new
cadence with ``$CADENCE <s>``.  State is exported for the ring buffer so a
reboot during an event resumes at the elevated rate.
"""
from __future__ import annotations

from dataclasses import dataclass

BASELINE, ELEVATED = "baseline", "elevated"


@dataclass
class DutyCycle:
    baseline_s: int = 3600
    elevated_s: int = 300
    cooldown_cycles: int = 12
    mode: str = BASELINE
    calm_streak: int = 0

    @property
    def cadence_s(self) -> int:
        return self.elevated_s if self.mode == ELEVATED else self.baseline_s

    def update(self, any_warning: bool) -> bool:
        """Advance one cycle; returns True if the cadence changed."""
        before = self.cadence_s
        if any_warning:
            self.mode, self.calm_streak = ELEVATED, 0
        elif self.mode == ELEVATED:
            self.calm_streak += 1
            if self.calm_streak >= self.cooldown_cycles:
                self.mode, self.calm_streak = BASELINE, 0
        return self.cadence_s != before

    def export_state(self) -> dict:
        return {"mode": self.mode, "calm_streak": self.calm_streak}

    def import_state(self, d: dict | None) -> None:
        if d and d.get("mode") in (BASELINE, ELEVATED):
            self.mode, self.calm_streak = d["mode"], int(d.get("calm_streak", 0))
