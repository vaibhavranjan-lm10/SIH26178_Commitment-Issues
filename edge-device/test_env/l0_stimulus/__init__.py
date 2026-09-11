"""L0 physical stimulus generation for the PRAHARI test environment.

Produces ELECTRICAL-level stimulus (ADC counts / microvolts, GPIO pulse
edge trains, I2C register response frames, UART byte streams) that Renode
feeds into Code A's *actual* peripheral drivers, so Code A's real
acquisition -> two-point calibration -> median-of-5 -> range-gate ->
z-score -> instantaneous-derivation pipeline runs against it exactly as
it would against real hardware.

This is NOT the same as, and must not be confused with, the offline
training pipeline's synthetic data generator in
``code_b_head_node/training_pipeline/`` -- see this package's README.
"""
from .physical import VillageScenario, PhysicalState, physical_series, physical_state  # noqa: F401
from .transducers import (  # noqa: F401
    ADC_COUNTS_MAX, ADC_VREF_UV, TRANSDUCERS, POSITIONS, transducers_at, volts_to_count,
)
from .eeprom import build_eeprom_image  # noqa: F401
from .bundle import build_bundle  # noqa: F401
from .eeprom import calib_apply, calib_entries_for  # noqa: F401
