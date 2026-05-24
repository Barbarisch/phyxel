"""
Phyxel automated character/asset interaction validation pipeline.

Entry points:
    - tools/interaction_pipeline/cli.py    — scriptable CLI
    - .claude/commands/interaction-pipeline.md — chat skill

Modules:
    engine_lifecycle    Engine start/stop/probe/crash-detect controller
    sweep               IE-mode animation sweep + screenshot capture
    report              Per-frame telemetry record & serialization
    detectors           Deterministic symptom detectors (engine bug vs profile)
    tuner               LLM diagnostician with heuristic fallback
    cli                 Top-level orchestrator
"""

__all__ = []
__version__ = "0.1.0"
