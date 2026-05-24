"""Compare world_pos vs centroid per frame to separate pose drift from world translation."""
import json
import sys
from pathlib import Path

p = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(r"tools/interaction_pipeline/reports/chair_wood/20260519_221704/report.json")
d = json.loads(p.read_text(encoding="utf-8"))
hdr = f"{'clip':14s} {'t':>5s}  {'wp.x':>8s} {'wp.y':>8s} {'wp.z':>8s}   {'cx':>8s} {'cy':>8s} {'cz':>8s}"
print(hdr)
for f in d["frames"]:
    tel = f["telemetry"]
    wp = tel.get("world_pos") or {}
    c = tel.get("centroid") or {}
    print(f"{f['clip']:14s} {f['t']:5.2f}  "
          f"{wp.get('x',0):8.3f} {wp.get('y',0):8.3f} {wp.get('z',0):8.3f}   "
          f"{c.get('x',0):8.3f} {c.get('y',0):8.3f} {c.get('z',0):8.3f}")
