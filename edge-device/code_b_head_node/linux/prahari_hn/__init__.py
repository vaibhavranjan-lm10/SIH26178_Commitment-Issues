"""prahari_hn — Code B, head-node runtime, Linux side (QRB2210, Debian).

This package SHIPS on the UNO Q.  It implements blueprint §6.7's execution
order every inference cycle — normalise/mask → temporal encoder → optional
graph stage → nine heads → temperature scaling — plus §6.8 alerting, §6.4
risk-adaptive duty cycling, the ~30-day SQLite ring buffer and the uplink.

Runtime dependencies: numpy, onnxruntime (aarch64 CPU EP).  Nothing here
imports the offline training pipeline; the model reaches this code only as
the versioned ONNX artifact (see model_registry.py).
"""
__version__ = "0.1.0"
