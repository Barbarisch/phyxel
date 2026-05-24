"""Dump per-frame telemetry from the latest IE sweep to look for sliding."""
import json
import sys
from pathlib import Path

p = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(r"tools/interaction_pipeline/reports/chair_wood/20260519_221704/report.json")
d = json.load(open(p, encoding="utf-8"))
print(f"frames: {len(d['frames'])}")
print()
print(f"{'clip':14s} {'t':>5s}  {'cx':>8s} {'cy':>8s} {'cz':>8s}  {'yaw':>7s}  {'lfY':>7s} {'rfY':>7s}  {'state':>14s}")
prev = None
for f in d["frames"]:
    t = f["t"]; clip = f["clip"]
    tel = f["telemetry"]
    c = tel["centroid"]
    yaw = tel.get("facing_yaw")
    feet = tel.get("feet", {}) or {}
    lf = (feet.get("left") or {})
    rf = (feet.get("right") or {})
    state = tel.get("state", "?")
    cx, cy, cz = c["x"], c["y"], c["z"]
    lfy = lf.get("y"); rfy = rf.get("y")
    print(f"{clip:14s} {t:5.2f}  {cx:8.3f} {cy:8.3f} {cz:8.3f}  {yaw if yaw is not None else 0.0:7.2f}  {lfy if lfy is not None else 0.0:7.3f} {rfy if rfy is not None else 0.0:7.3f}  {state:>14s}")
    if prev is not None and prev[0] == clip:
        dx = cx - prev[1]; dy = cy - prev[2]; dz = cz - prev[3]
        dh = (dx*dx + dz*dz) ** 0.5
        if dh > 0.05 or abs(dy) > 0.05:
            print(f"  ^ DRIFT within {clip}: dxz={dh:.3f}, dy={dy:.3f} (dx={dx:.3f}, dz={dz:.3f})")
    prev = (clip, cx, cy, cz)
