#!/usr/bin/env python3
"""ambient_model_check.py — deterministic red/green check of the AMBIENT lighting model.

WHY THIS EXISTS. The traced 5-ray sky visibility (M3) made an exterior wall facing a neighbour
13 u away read 0.39 sky access, squared into a 5.6x darker ambient, i.e. black (Ravenmere G-141,
2026-09-17). Nothing measured that case: the lighting lab has rooms (sealed / door / window) but no
EXTERIOR case, and every earlier reading of the debug views went through the AgX tone curve, which
hid the gap. This script measures the ambient model on the lab world with the tone curve OFF and
asserts the invariants an ambient model must satisfy:

  A1  CONTINUITY   A wall with another wall 13 u in front of it keeps >= 40% of the ambient of the
                   same wall standing alone. (The 5-ray trace gives 0.15 -> RED before the change.)
  A2  SEALED       A sealed room's interior reads <= 8% of the open-roof control room's interior.
                   The control's region is its SUNLIT floor at noon; the sealed room can only carry
                   the model's ambient floor (kAmbientFloorAtmos = 0.02 of sky), which measures
                   0.073 of that (the same 0.0176-0.0188 linear every ambient model has produced
                   here). Above 0.08 light is getting in through the walls -- the trace read 0.079,
                   probes WITHOUT the visibility test 0.114.
  A3  RESOLUTION   A room sealed by a ONE-MICRO-THICK roof reads within 1.5x of the cube-roofed
                   sealed room and also passes A2. Occlusion is a property of matter, not of the
                   voxel size that stores it.
  A4  OPENING      The door room's far wall (wall band) reads >= 1.5x the sealed room's. Grounded estimate: a
                   1x2 opening seen from 5 u subtends ~2/(pi*25) = 2.5% of the hemisphere, so that
                   wall should carry ~2.5% of open sky on top of the 2% floor -- about 2.2x sealed.
                   Equal to sealed (the fixed-direction field: 1.00; the trace: 1.15) is the failure.

RIG (test-rig discipline, CLAUDE.md): the lighting lab rooms (tools/lighting_lab.py, hand-placed via
/api/world/fill, one variable each) plus, built here and labelled the same way:
  wall_open      one 7x5 StoneBricks slab at x 62..68, z 4, nothing within 16 u in front (+Z).
  wall_street    the same slab at x 78..84, z 4, with an identical facing slab at z 17 (13 u away —
                 the Ravenmere distance).
  sealed_micro   the lab's sealed room geometry at x 60..66, z 22..28, but its roof is a single
                 1/9-u microcube layer instead of a cube layer.
Walls are measured in debug view 7 (the ambient term alone, voxel.frag), rooms in the normal view,
all at NOON (clock paused) with POST /api/debug/tonemap {curve:0, exposure:16}; the original tonemap is
restored after (linear x16 so a shaded wall spans real 8-bit range; ratios are what is asserted).

USAGE (editor running on the StructGenTest project, port 8090):
  python tools/ambient_model_check.py --build           # terrain + lab rooms + the rig above
  python tools/ambient_model_check.py --check <tag>     # capture, measure, assert; exit 1 on FAIL
Captures: docs/evidence/ambient_<tag>_<pose>.png; numbers: docs/evidence/ambient_<tag>.json.
"""
from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

import numpy as np
from PIL import Image

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))
import lighting_lab as lab  # noqa: E402  (post/get/job/fill/set_camera/ROOMS/build/poses)

EVIDENCE = TOOLS.parent / "docs" / "evidence"
WALL_LO, WALL_HI, ROOF_Y = lab.WALL_LO, lab.WALL_HI, lab.ROOF_Y

# ── the rig ──────────────────────────────────────────────────────────────────────────────────────
WALL_OPEN = dict(x0=62, x1=68, z=4)
WALL_STREET = dict(x0=78, x1=84, z=4, z_facing=17)
MICRO_ROOM = dict(x0=60, x1=66, z0=22, z1=28)

# Screen regions (fractions of width/height). Walls fill the frame centre at the poses below.
REGION_WALL = [0.35, 0.30, 0.65, 0.70]
REGION_ROOM = lab.REGIONS["interior"]
# The far WALL of a room seen from just inside its -Z wall (pitch -5): the band above the floor
# line. A4's estimate is for the wall facing the door; the floor band under it is grass, which at
# noon carries its own (sun) story and would dilute the wall's reading 3:1.
REGION_ROOM_WALL = [0.30, 0.28, 0.70, 0.55]


def build_rig() -> None:
    lab.fill(WALL_OPEN["x0"], WALL_LO, WALL_OPEN["z"], WALL_OPEN["x1"], WALL_HI, WALL_OPEN["z"], "StoneBricks")
    lab.fill(WALL_STREET["x0"], WALL_LO, WALL_STREET["z"], WALL_STREET["x1"], WALL_HI, WALL_STREET["z"], "StoneBricks")
    lab.fill(WALL_STREET["x0"], WALL_LO, WALL_STREET["z_facing"], WALL_STREET["x1"], WALL_HI, WALL_STREET["z_facing"], "StoneBricks")
    m = MICRO_ROOM
    lab.fill(m["x0"], WALL_LO, m["z0"], m["x1"], WALL_HI, m["z0"], "StoneBricks")
    lab.fill(m["x0"], WALL_LO, m["z1"], m["x1"], WALL_HI, m["z1"], "StoneBricks")
    lab.fill(m["x0"], WALL_LO, m["z0"], m["x0"], WALL_HI, m["z1"], "StoneBricks")
    lab.fill(m["x1"], WALL_LO, m["z0"], m["x1"], WALL_HI, m["z1"], "StoneBricks")
    # The roof: the BOTTOM micro layer (my = 0 of subcube sy = 0) of every cell at ROOF_Y.
    micro = []
    for x in range(m["x0"], m["x1"] + 1):
        for z in range(m["z0"], m["z1"] + 1):
            for sx in range(3):
                for sz in range(3):
                    for mx in range(3):
                        for mz in range(3):
                            micro.append({"x": x, "y": ROOF_Y, "z": z, "sx": sx, "sy": 0, "sz": sz,
                                          "mx": mx, "my": 0, "mz": mz, "material": "StoneBricks"})
    for i in range(0, len(micro), 500):
        lab.post("/api/world/microcubes/batch", {"microcubes": micro[i:i + 500]})
    time.sleep(1.0)


def verify_rig() -> bool:
    """Read the WORLD back (never the fill response)."""
    ok = True
    m = MICRO_ROOM
    r = lab.post("/api/world/scan_micro", {"x1": m["x0"], "y1": ROOF_Y, "z1": m["z0"],
                                           "x2": m["x1"], "y2": ROOF_Y, "z2": m["z1"]})
    cells = r.get("cells", [])
    micro_total = sum(c.get("counts", {}).get("StoneBricks", 0) for c in cells)
    want = 81 * (m["x1"] - m["x0"] + 1) * (m["z1"] - m["z0"] + 1)
    print(f"  sealed_micro roof: {micro_total}/{want} micro cells ({len(cells)} cells)")
    ok &= micro_total == want
    for name, w in (("wall_open", WALL_OPEN), ("wall_street", WALL_STREET)):
        got = sum(1 for x in range(w["x0"], w["x1"] + 1) for y in range(WALL_LO, WALL_HI + 1)
                  if lab.get("/api/world/voxel", {"x": x, "y": y, "z": w["z"]}).get("exists"))
        want_w = (w["x1"] - w["x0"] + 1) * (WALL_HI - WALL_LO + 1)
        print(f"  {name:<12} slab: {got}/{want_w} cubes")
        ok &= got == want_w
    return ok


def poses() -> list[tuple[str, dict, int, list[float]]]:
    """(name, camera, debug mode, region). Yaw: 0 = +X, 90 = +Z, -90 = -Z (lighting_lab.poses)."""
    out = []
    cx = (WALL_OPEN["x0"] + WALL_OPEN["x1"]) / 2 + 0.5
    out.append(("wall_open", {"x": cx, "y": WALL_LO + 2.5, "z": WALL_OPEN["z"] + 6.0,
                              "yaw": -90.0, "pitch": 0.0, "mode": "free"}, 7, REGION_WALL))
    cx = (WALL_STREET["x0"] + WALL_STREET["x1"]) / 2 + 0.5
    out.append(("wall_street", {"x": cx, "y": WALL_LO + 2.5, "z": WALL_STREET["z"] + 6.0,
                                "yaw": -90.0, "pitch": 0.0, "mode": "free"}, 7, REGION_WALL))
    for name, cam in lab.poses():
        if name in ("in_control_open", "in_sealed", "in_door"):
            out.append((name, cam, 0, REGION_ROOM))
        if name in ("in_sealed", "in_door"):
            out.append((name + "_wall", cam, 0, REGION_ROOM_WALL))
    m = MICRO_ROOM
    out.append(("in_sealed_micro", {"x": (m["x0"] + m["x1"]) // 2 + 0.5, "y": WALL_LO + 1.5,
                                    "z": m["z0"] + 1.5, "yaw": 90.0, "pitch": -5.0, "mode": "free"},
                0, REGION_ROOM))
    return out


def srgb_to_linear(x: np.ndarray) -> np.ndarray:
    return np.where(x <= 0.04045, x / 12.92, ((x + 0.055) / 1.055) ** 2.4)


def region_lum(path: Path, region: list[float]) -> float:
    a = np.asarray(Image.open(path).convert("RGB"), dtype=np.float32) / 255.0
    h, w = a.shape[:2]
    x0, y0, x1, y1 = int(region[0] * w), int(region[1] * h), int(region[2] * w), int(region[3] * h)
    lin = srgb_to_linear(a[y0:y1, x0:x1])
    return float((0.2126 * lin[..., 0] + 0.7152 * lin[..., 1] + 0.0722 * lin[..., 2]).mean())


def check(tag: str) -> int:
    lab.require_engine()
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    before = lab.post("/api/debug/tonemap", {})
    # Noon, clock stopped: the ambient level must be the same in every run, and at exposure 1 the
    # ambient of a shaded wall lands on 1-3 8-bit steps, so measure at exposure 16 (curve OFF, so
    # the readout is linear x16 and ratios are exact; nothing measured here comes near clipping).
    lab.post("/api/daynight/set", {"enabled": True, "paused": True, "timeOfDay": 12.0})
    lab.post("/api/debug/tonemap", {"curve": 0, "exposure": 16.0})
    time.sleep(2.5)
    settle = 6.0   # the probe field blends over refreshes (1/8 grid per frame, kBlend 0.3): let it converge
    vals: dict[str, float] = {}
    try:
        for name, cam, mode, region in poses():
            lab.set_camera(cam)
            lab.post("/api/debug/shadow", {"mode": mode})
            time.sleep(settle)
            res = lab.get("/api/screenshot")
            src = res.get("path")
            out = EVIDENCE / f"ambient_{tag}_{name}.png"
            out.write_bytes(Path(src).read_bytes())
            vals[name] = region_lum(out, region)
            print(f"  {name:<18} mode {mode}  linear luminance {vals[name]:.5f}")
    finally:
        lab.post("/api/debug/shadow", {"mode": 0})
        lab.post("/api/debug/tonemap", {"curve": before.get("curve", 1), "exposure": before.get("exposure", 8.0)})

    eps = 1e-6
    results = [
        ("A1 continuity: wall_street / wall_open >= 0.40",
         vals["wall_street"] / (vals["wall_open"] + eps), lambda v: v >= 0.40),
        ("A2 sealed: in_sealed / in_control_open <= 0.08",
         vals["in_sealed"] / (vals["in_control_open"] + eps), lambda v: v <= 0.08),
        ("A3 resolution: in_sealed_micro / in_sealed <= 1.5",
         vals["in_sealed_micro"] / (vals["in_sealed"] + eps), lambda v: v <= 1.5),
        ("A3 resolution: in_sealed_micro / in_control_open <= 0.08",
         vals["in_sealed_micro"] / (vals["in_control_open"] + eps), lambda v: v <= 0.08),
        ("A4 opening: in_door_wall / in_sealed_wall >= 1.5",
         vals["in_door_wall"] / (vals["in_sealed_wall"] + eps), lambda v: v >= 1.5),
    ]
    failed = 0
    print()
    for label, v, pred in results:
        ok = pred(v)
        failed += 0 if ok else 1
        print(f"  {'PASS' if ok else 'FAIL'}  {label}   measured {v:.4f}")
    (EVIDENCE / f"ambient_{tag}.json").write_text(json.dumps(
        {"tag": tag, "values": vals, "results": [{"check": l, "measured": v, "pass": p(v)} for l, v, p in results]},
        indent=2), encoding="utf-8")
    print(f"\n{'RED' if failed else 'GREEN'}: {failed} failing check(s); numbers in {EVIDENCE / f'ambient_{tag}.json'}")
    return 1 if failed else 0


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build", action="store_true", help="generate terrain, the lab rooms and the rig")
    ap.add_argument("--check", metavar="TAG", help="capture + measure + assert")
    args = ap.parse_args(argv)
    if args.build:
        lab.build(with_tavern=False)
        print("building the ambient rig (hand-placed via /api/world/fill + microcubes/batch)...")
        build_rig()
        print("verifying the rig from the world:")
        if not verify_rig():
            print("RIG VERIFY FAILED"); return 2
        if lab.verify() != 0:
            print("LAB VERIFY FAILED"); return 2
    if args.check:
        return check(args.check)
    return 0


if __name__ == "__main__":
    sys.exit(main())
