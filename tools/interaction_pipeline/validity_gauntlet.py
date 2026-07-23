"""Interaction Validity Gauntlet — instrument #1: transition penetration trace.

Measures where the body ACTUALLY is (docs/CharacterLibraryPlan.md, mandated
2026-07-22): for each (preset x seat) pair the fit gate allows, run the full
sit -> settle -> stand cycle while sampling world-space bone AABBs, and
measure per-sample interpenetration of the body vs the seat's SOLID voxel
geometry and the floor. Then check final-pose contracts numerically.

PASS/FAIL is measured, not asserted from clip names or API flags.

Thresholds (calibrated against the known-good standard x chair_wood case and
the known-bad quarantined hop; see --calibrate):
  CONTACT_TOL   — overlap depth that counts as normal seated contact
  FAIL_DEPTH    — max penetration depth beyond which the pose is invalid
  SEAT_POSE_TOL — |pelvis_bottom - seat_top| tolerance for "actually seated"

Usage (engine running, CharacterTestbed):
    python tools/interaction_pipeline/validity_gauntlet.py            # matrix
    python tools/interaction_pipeline/validity_gauntlet.py --calibrate
    python tools/interaction_pipeline/validity_gauntlet.py --preset halfling \
        --seat stool --experimental-hop    # reproduce the known-bad case
"""
from __future__ import annotations

import argparse
import json
import sys
import time
import urllib.request
from pathlib import Path

BASE = "http://localhost:8090"
REPO = Path(__file__).resolve().parents[2]
SPOT = (124, 17, 124)

CONTACT_TOL = 0.14    # normal seated contact (pelvis/thigh boxes meet seat top)
FAIL_DEPTH = 0.28     # deeper than this = body inside the object
# Calibrated 2026-07-22 from the measured known-good case (standard x
# chair_wood: pelvis_err 0.254 — the engine anchors the Hips bone CENTER on
# the seat surface, so the bone's bottom face sits ~a half-extent below it).
SEAT_POSE_TOL = 0.32
FLOOR_TOL = 0.15      # bone bottom below floor by more than this = failure

# Bones measured against the seat (arms swing freely; hands excluded).
MEASURED = ("hips", "spine", "head", "upleg", "leg")

PRESETS = ["halfling", "gnome", "goblin", "dwarf", "standard", "elf",
           "half_orc", "goliath", "ogre"]
SEATS = ["stool_low", "stool", "chair_wood", "bench_wood", "bar_stool",
         "bench_great"]


def post(p, b, timeout=90):
    req = urllib.request.Request(BASE + p, json.dumps(b).encode(),
                                 {"Content-Type": "application/json"})
    try:
        return json.load(urllib.request.urlopen(req, timeout=timeout))
    except Exception as e:
        return {"error": str(e)}


def get(p, timeout=30):
    return json.load(urllib.request.urlopen(BASE + p, timeout=timeout))


def seat_cells(template: str, origin):
    """World-space solid cells (min,max per cell) of a placed seat template."""
    path = REPO / "resources" / "templates" / f"{template}.voxel"
    cells = []
    for ln in path.read_text(encoding="utf-8").splitlines():
        p = ln.split()
        if not p:
            continue
        if p[0] == "C" and len(p) >= 5:
            x, y, z, s = int(p[1]), int(p[2]), int(p[3]), 1.0
        elif p[0] == "S" and len(p) >= 8:
            x = int(p[1]) + int(p[4]) / 3
            y = int(p[2]) + int(p[5]) / 3
            z = int(p[3]) + int(p[6]) / 3
            s = 1 / 3
        elif p[0] == "M" and len(p) >= 11:
            x = int(p[1]) + int(p[4]) / 3 + int(p[7]) / 9
            y = int(p[2]) + int(p[5]) / 3 + int(p[8]) / 9
            z = int(p[3]) + int(p[6]) / 3 + int(p[9]) / 9
            s = 1 / 9
        else:
            continue
        mn = (origin[0] + x, origin[1] + y, origin[2] + z)
        cells.append((mn, (mn[0] + s, mn[1] + s, mn[2] + s)))
    return cells


def bones(entity_id):
    r = get(f"/api/entity/{entity_id}/bones")
    out = []
    for b in r.get("bones", []):
        n = b["name"].lower()
        if any(k in n for k in MEASURED) and "hand" not in n:
            c, h = b["center"], b["half"]
            out.append((b["name"],
                        (c[0] - h[0], c[1] - h[1], c[2] - h[2]),
                        (c[0] + h[0], c[1] + h[1], c[2] + h[2])))
    return out


def penetration(bone_min, bone_max, cells):
    """Max penetration depth (m) of a bone AABB into any solid cell."""
    worst = 0.0
    for mn, mx in cells:
        depths = []
        for i in range(3):
            lo = max(bone_min[i], mn[i])
            hi = min(bone_max[i], mx[i])
            if hi <= lo:
                depths = None
                break
            depths.append(hi - lo)
        if depths is not None:
            worst = max(worst, min(depths))
    return worst


def trace(entity_id, cells, floor_y, seconds):
    """Sample bone penetration for `seconds`; return worst sample."""
    t0 = time.time()
    worst = {"depth": 0.0, "bone": None, "t": 0.0, "floor_breach": 0.0}
    while time.time() - t0 < seconds:
        for name, mn, mx in bones(entity_id):
            d = penetration(mn, mx, cells)
            if d > worst["depth"]:
                worst.update(depth=d, bone=name.replace("mixamorig:", ""),
                             t=round(time.time() - t0, 2))
            breach = floor_y - mn[1]
            if breach > worst["floor_breach"]:
                worst["floor_breach"] = round(breach, 3)
        time.sleep(0.15)
    return worst


def run_pair(preset, seat_tmpl, seat_id, seat_origin, experimental_hop=False):
    name = f"VG_{preset}"
    spec = {"name": name, "position": {"x": seat_origin[0] + 0.3,
                                       "y": seat_origin[1] + 1,
                                       "z": seat_origin[2] - 2.5}}
    if preset != "standard":
        spec["appearance"] = {"preset": preset}
    post("/api/npc/spawn", spec)
    time.sleep(1.5)
    eid = f"npc_{name}"

    fit = post("/api/interaction/can_interact",
               {"entity_id": eid, "object_id": seat_id})
    if not fit.get("can_interact"):
        post("/api/npc/remove", {"name": name})
        return {"skip": "fit-refused"}

    cells = seat_cells(seat_tmpl, seat_origin)
    floor_y = seat_origin[1]

    body = {"entity_id": eid, "object_id": seat_id}
    if experimental_hop:
        body["experimental_hop"] = True
    sit = post("/api/interaction/sit", body)
    if not sit.get("success"):
        post("/api/npc/remove", {"name": name})
        return {"skip": f"sit-failed: {sit.get('error')}"}

    # Transition + settle window (hop clip 3.9s; stand_to_sit 2.2s).
    worst = trace(eid, cells, floor_y, 5.5)

    # Final-pose contract: pelvis bottom vs seat top.
    seat_top = None
    metrics = json.loads((REPO / "resources" / "templates" /
                          f"{seat_tmpl}.metrics.json").read_text(encoding="utf-8"))
    for p in metrics.get("interaction_points", []):
        f = p.get("features") or {}
        if f.get("seat_top_y") is not None:
            seat_top = seat_origin[1] + f["seat_top_y"]
    pelvis_err = None
    for bname, mn, mx in bones(eid):
        if bname.lower().endswith("hips"):
            pelvis_err = round(abs(mn[1] - seat_top), 3) if seat_top else None

    post("/api/interaction/stand_up", {"entity_id": eid})
    stand_worst = trace(eid, cells, floor_y, 2.5)
    post("/api/npc/remove", {"name": name})

    fail = []
    if worst["depth"] > FAIL_DEPTH:
        fail.append(f"sit penetration {worst['depth']:.3f} ({worst['bone']} @{worst['t']}s)")
    if worst["floor_breach"] > FLOOR_TOL:
        fail.append(f"floor breach {worst['floor_breach']:.3f}")
    if pelvis_err is not None and pelvis_err > SEAT_POSE_TOL:
        fail.append(f"pelvis-seat error {pelvis_err:.3f}")
    if stand_worst["depth"] > FAIL_DEPTH:
        fail.append(f"stand penetration {stand_worst['depth']:.3f}")

    return {"pen": round(worst["depth"], 3), "bone": worst["bone"],
            "pelvis_err": pelvis_err, "floor": worst["floor_breach"],
            "fail": fail}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--preset", default=None)
    ap.add_argument("--seat", default=None)
    ap.add_argument("--experimental-hop", action="store_true")
    ap.add_argument("--calibrate", action="store_true",
                    help="run known-good and known-bad; instrument must discriminate")
    args = ap.parse_args()

    def place(tmpl, dx):
        r = post("/api/world/template",
                 {"name": tmpl, "position": {"x": SPOT[0] + dx, "y": SPOT[1],
                                             "z": SPOT[2]}, "static": True})
        # The engine may snap placement (terrain height) — measure against
        # where the seat ACTUALLY is, never where we asked for it.
        p = r.get("position") or {}
        origin = (p.get("x", SPOT[0] + dx), p.get("y", SPOT[1]), p.get("z", SPOT[2]))
        return r.get("object_id"), origin

    if args.calibrate:
        cid, corigin = place("chair_wood", 0)
        good = run_pair("standard", "chair_wood", cid, corigin)
        sid, sorigin = place("stool", 6)
        bad = run_pair("halfling", "stool", sid, sorigin, experimental_hop=True)
        for oid in (cid, sid):
            post("/api/placed_object/remove", {"id": oid})
        print(f"KNOWN-GOOD standard x chair_wood: {good}")
        print(f"KNOWN-BAD  halfling x stool (hop): {bad}")
        # A skipped case is NOT a passed case — the instrument must have
        # actually MEASURED the good pair (pen is a number) and measured the
        # bad pair as failing. Vacuous truth = invalid instrument.
        good_measured = "skip" not in good and good.get("pen") is not None
        ok = good_measured and not good.get("fail") and bool(bad.get("fail"))
        print("INSTRUMENT " + ("VALID — discriminates good from bad" if ok
                               else "INVALID — cannot discriminate (good case "
                                    "not measured or verdicts wrong)"))
        sys.exit(0 if ok else 1)

    presets = [args.preset] if args.preset else PRESETS
    seats = [args.seat] if args.seat else SEATS
    failures = []
    for tmpl in seats:
        oid, origin = place(tmpl, 0)
        if not oid:
            print(f"{tmpl}: spawn failed, skipping")
            continue
        for preset in presets:
            r = run_pair(preset, tmpl, oid, origin,
                         experimental_hop=args.experimental_hop)
            if "skip" in r:
                print(f"{preset:9s} x {tmpl:12s} SKIP ({r['skip']})")
                continue
            verdict = "PASS" if not r["fail"] else "FAIL " + "; ".join(r["fail"])
            print(f"{preset:9s} x {tmpl:12s} pen={r['pen']:.3f}({r['bone']}) "
                  f"pelvis_err={r['pelvis_err']} {verdict}")
            if r["fail"]:
                failures.append((preset, tmpl, r["fail"]))
        post("/api/placed_object/remove", {"id": oid})

    print("\nRESULT:", "ALL MEASURED PAIRS VALID" if not failures
          else f"{len(failures)} INVALID PAIRS: {failures}")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
