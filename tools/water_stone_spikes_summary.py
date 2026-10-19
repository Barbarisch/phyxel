"""Summarise water_stone_spikes.py runs (26.32): per run, the tallest the field stood above the pond, how long it stood more than
30 cm above it, the largest corner-to-corner height in one 11 cm quad, and the over-full carry in litres (made in a body's cells
without / with a displacement rate, elsewhere, parked at a ceiling).   python tools/water_stone_spikes_summary.py <tag> [<tag> ...]"""
import json, pathlib, sys
EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_h1"
for tag in sys.argv[1:]:
    rows = json.loads((EV / f"{tag}.json").read_text())["rows"]
    if not rows: print(f"{tag:24s} no rows"); continue
    peak = max(r["field_rise"] for r in rows); quad = max(r["quad_span"] for r in rows)
    over = sum(b["t"] - a["t"] for a, b in zip(rows, rows[1:]) if a["field_rise"] > 0.3)
    late = max((r["field_rise"] for r in rows if r["t"] > 4.0), default=0.0)
    c = (rows[-1].get("carry_l", []) + [0, 0, 0, 0])[:4]
    print(f"{tag:24s} peak {peak:5.2f} m  >30 cm for {over:4.2f} s  after 4 s {late:4.2f} m  quad {quad:4.2f} m  carry L body {c[0]:7.1f} rate {c[3]:7.1f} flow {c[1]:7.1f} parked {c[2]:6.1f}")
