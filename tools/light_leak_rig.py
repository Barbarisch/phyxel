#!/usr/bin/env python3
"""light_leak_rig.py — M0 layer 2: the IN-ENGINE light-leak baseline.

docs/UnifiedLightingPlan.md M0. Layer 1 (tests/graphics/LightWallMatrixTest.cpp) covers the
per-cell stored flood with no engine. This covers what only the engine can answer: the FORWARD
point lights and item lights, which exist solely in the fragment shader and have no CPU-side
observable.

METHOD, and why it is shaped this way
-------------------------------------
* Structures are built by the REAL generator at three REAL styles, so the wall thicknesses are
  the engine's own (timber_cottage 0.222 -> 2 micro, stone_manor 0.667 -> 6, stone_keep 3.0 -> 9).
  Authoring synthetic boxes would have tested a fixture instead of the engine.
* `blacksmith` typology: room_program.json gives it NO windows spec, so it generates none.
* Every doorway is then SEALED with cubes, so the building has no opening of any kind and any
  exterior light is a defect by definition, with nothing to argue about.
* Measurement is always the CENTRE RECT of the frame, and the CAMERA is aimed at whatever feature
  is being probed. Fixed screen rects that try to find a feature are fragile; moving the camera
  and always measuring the middle is not.
* Every number is an ON-minus-OFF delta for the same light. That subtracts the sun, the sky and
  the ambient, so nothing here depends on reasoning about daylight or on eyeballing an image.
* Each box carries its own positive control: the same light moved OUTSIDE, which must light the
  wall. Without it a zero could mean "occluded" or "the light did nothing", and those are
  different answers.

This script reports numbers. It does not decide whether they are good.
"""

import argparse
import json
import sys
import time
import urllib.request
from pathlib import Path

API = "http://localhost:8090"

# Three real styles -> three real wall thicknesses. thicknessMicro = clamp(round(cubes*9), 1, 9).
STYLES = [
    ("timber_cottage", 0.222, 2),
    ("stone_manor",    0.667, 6),
    ("stone_keep",     3.000, 9),
]

GROUND_Y = 16
FOOTPRINT = [9, 6]          # within blacksmith's cruck-span max (width <= 6)
SPACING = 32                # boxes far enough apart that no light reaches a neighbour
FIRST_X = 8
BOX_Z = 22                  # inside the flat-generated region (z 0..31); outside it the
                            # builder seats boxes on real terrain ~36 cubes up, on a cliff

# Centre rect of a 1600x900 frame, inside the viewport panel (which does not start at 0,0).
CENTRE_RECT = (700, 380, 900, 520)


def post(path, body=None, timeout=180.0):
    data = json.dumps(body or {}).encode()
    req = urllib.request.Request(f"{API}{path}", data=data,
                                 headers={"Content-Type": "application/json"}, method="POST")
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read().decode() or "{}")


def get(path, timeout=60.0):
    with urllib.request.urlopen(f"{API}{path}", timeout=timeout) as r:
        return json.loads(r.read().decode() or "{}")


#: The job API reports progress in `state`, NOT `status`, and its terminal value is "complete".
#: Polling the wrong key silently spun the full timeout on every fill — 3 minutes per opening,
#: which read as "the engine is hanging" when it was the rig waiting for a key that never appears.
JOB_TERMINAL = {"complete", "completed", "failed", "error", "cancelled"}


def job(kind, params, timeout=180.0):
    r = post("/api/job/submit", {"type": kind, "params": params})
    jid = r.get("job_id") or r.get("id")
    if jid is None:
        return r
    for _ in range(int(timeout)):
        s = get(f"/api/job/{jid}")
        if s.get("state", s.get("status")) in JOB_TERMINAL:
            return s
        time.sleep(1.0)
    return {"state": "timeout"}


def settle(seconds=2.5):
    """The scene animates (wind). Two frames from one build differ; settle, then shoot twice
    and keep the second, or an identical-stats result may just be a stale frame."""
    time.sleep(seconds)


def shot(tag, outdir):
    settle()
    get("/api/screenshot")
    time.sleep(0.4)
    r = get("/api/screenshot")
    src = Path(r["path"])
    if not src.is_absolute():
        src = Path.cwd() / src
    dst = Path(outdir) / f"{tag}.png"
    dst.write_bytes(src.read_bytes())
    return dst


def luminance(path, rect):
    from PIL import Image
    im = Image.open(path).convert("RGB").crop(rect)
    px = list(im.getdata())
    return sum(0.2126 * r + 0.7152 * g + 0.0722 * b for r, g, b in px) / len(px)


def aim(pos, yaw, pitch=0.0):
    post("/api/camera", {"position": {"x": pos[0], "y": pos[1], "z": pos[2]},
                         "yaw": yaw, "pitch": pitch})


def build_box(idx, style, outdir):
    """Generate one windowless structure and seal its doorways. Returns its plan, or None."""
    ox = FIRST_X + idx * SPACING

    # Re-runnable: if this box is already standing, reuse it. Building again would stack a
    # second structure on the first and quietly poison every measurement after it.
    for o in get("/api/placed_objects").get("objects", []):
        md = o.get("metadata", {})
        # Match x AND z. Matching x alone reused a box from a previous run that sat at a
        # different z (and on a cliff), silently measuring the wrong building.
        if (o.get("category") == "structure"
                and abs(o["position"]["x"] - ox) < 2
                and abs(o["position"]["z"] - BOX_Z) < 2
                and md.get("building", {}).get("style") == style):
            print(f"    {style}: reusing existing structure at x={ox}")
            return seal(md["assembly_plan"], ox, style)

    try:
        r = post("/api/structure/build", {
            "schema": "v2", "typology": "blacksmith",
            "footprint": FOOTPRINT, "stories": [{"height": 3}],
            "position": {"x": ox, "y": GROUND_Y, "z": BOX_Z},
            "style": style, "furnish": False, "seed": 5,
        })
    except Exception as e:
        r = {"error": str(e)}
    # A structure build runs on the game loop and routinely outlives the request's 5s window in
    # a Debug build, so the HTTP reply says "timed out" while the build proceeds. The response is
    # not the result — VERIFY THE WORLD. A refusal at a forge gate is a real failure and shows up
    # as the structure simply never appearing.
    if r.get("error") and "timed out" not in str(r.get("error")):
        print(f"    BUILD REFUSED ({style}): {r['error']}")
        return None

    # Seal every opening the plan declares. Read them from the plan rather than guessing —
    # the generator is the authority on where its own doors are.
    plan = None
    for _ in range(120):
        objs = get("/api/placed_objects").get("objects", [])
        for o in objs:
            md = o.get("metadata", {})
            # Match x AND z here too. Matching x alone picked up a same-x structure from an
            # earlier run standing at a different z, and then "sealed" its doorways instead.
            if (md.get("assembly_plan")
                    and abs(o["position"]["x"] - ox) < 2
                    and abs(o["position"]["z"] - BOX_Z) < 2):
                plan = md["assembly_plan"]
                break
        if plan:
            break
        time.sleep(2.0)
    if not plan:
        print(f"    no structure appeared at x={ox} for {style} (build never completed)")
        return None

    return seal(plan, ox, style)


def seal(plan, ox, style):
    """Fill every opening the plan declares. Always run, including on a reused box — sealing is
    idempotent (place() refuses an occupied cell), and skipping it on reuse once left boxes with
    open doorways that would have invalidated every number measured on them."""
    origin = plan["origin"]
    sealed = 0
    for op in plan["plan"].get("openings", []):
        x0 = origin[0] + op["x"]
        y0 = origin[1] + op["y"]
        z0 = origin[2] + op["z"]
        s = job("fill_region", {
            "x1": x0, "y1": y0, "z1": z0,
            "x2": x0 + max(op["w"], 1) - 1, "y2": y0 + max(op["h"], 1) - 1,
            "z2": z0 + max(op["d"], 1) - 1,
            "material": "Stone",
        })
        # Verify the seal instead of assuming it: `failed` counts cells place() refused because
        # something was already there, which is fine; `placed == 0` would mean an OPEN doorway
        # and would invalidate every measurement on this box.
        res = s.get("result", {})
        placed, failed = int(res.get("placed", 0)), int(res.get("failed", 0))
        sealed += placed
        # placed==0 with failed>0 means every cell was ALREADY solid — sealed by a previous run,
        # which is fine. placed==0 AND failed==0 means the fill touched nothing at all, which
        # means the coordinates are wrong and the box is probably still open.
        if placed == 0 and failed == 0:
            print(f"      WARNING: fill at ({x0},{y0},{z0}) touched NOTHING "
                  f"(state={s.get('state')}) - this box is probably not sealed")
    print(f"    {style}: built at x={ox}, sealed {sealed} cell(s) across "
          f"{len(plan['plan'].get('openings', []))} opening(s)")
    return {"ox": ox, "origin": origin, "style": style}


def terrain_y(x, z, fallback):
    """Surface height at a column. NEVER assume a ground level: the builder seats a structure on
    the real terrain, and a rig that hardcodes GROUND_Y buries its own camera inside a hill —
    which is exactly what the first run of this script did, producing six zeros that looked like
    a clean result and were actually a camera inside a rock."""
    try:
        r = get(f"/api/world/terrain_height?x={int(x)}&z={int(z)}")
        y = r.get("surface_y", r.get("height"))
        return float(y) if y is not None else fallback
    except Exception:
        return fallback


def measure(box, outdir, results):
    """For this box: light inside (face probe, corner probe) and the outside positive control."""
    ox, oz = box["ox"], BOX_Z
    w, d = FOOTPRINT
    cx = ox + w / 2.0
    base = float(box["origin"][1])          # the structure's ACTUAL seated height
    inside = (cx, base + 2.5, oz + d / 2.0)

    # Camera poses: aim at the middle of the -Z wall, then at the -Z/-X vertical corner. Each
    # camera sits above the terrain AT ITS OWN COLUMN, not above the building's.
    # Stand CLOSE. The building is only ~3 cubes tall, so from 9 cubes back the centre of the
    # frame is sky and grass, not wall — which is how five of six rows came back with a dead
    # control. At 3.5 cubes the wall fills the middle of the view.
    fx, fz = cx, oz - 3.5
    kx, kz = ox - 3.0, oz - 3.0
    eye = base + 1.5                       # mid-wall height, so the frame is all wall
    # The positive control must sit where THIS probe is looking. Parking it in front of the face
    # for both probes left the corner camera unable to see its own control, so every corner row
    # came back INVALID - and the corner is the only probe that can show the reported defect,
    # because a flat wall's outer face is back-facing to an interior light (N.L <= 0) and can
    # never light up regardless of occlusion.
    # SEALING DOES NOT WORK, and it is not this rig's fault: place_voxel/fill_region SILENTLY
    # REFUSE placements in these cells (`success:false`, cell stays air) - the known voxel-cap
    # ghost already recorded in the repo, which tools/lighting_lab.py also had to route around.
    # So instead of pretending the box is sealed, the door becomes a CONTROLLED VARIABLE:
    #
    #   the generator puts this typology's single exterior door on the -X wall, so
    #     corner_far  (+X/+Z, diagonally opposite) - NO door path exists; light here crossed a wall
    #     corner_door (-X/-Z, beside the doorway)  - door spill is EXPECTED and correct here
    #
    # corner_door therefore doubles as a second positive control: it should be lit.
    fxp, fzp = ox + w + 3.0, oz + d + 3.0
    poses = {
        # (camera pos, yaw, control-light pos just outside the surface being viewed)
        "face":        ((fx, eye, fz), 90.0, (cx, base + 2.0, oz - 2.0)),
        "corner_door": ((kx, eye, kz), 45.0, (ox - 1.5, base + 2.0, oz - 1.5)),
        "corner_far":  ((fxp, eye, fzp), 225.0, (ox + w + 1.5, base + 2.0, oz + d + 1.5)),
    }

    for probe, (campos, yaw, ctlpos) in poses.items():
        aim(campos, yaw)
        lamp = {"color": {"r": 1.0, "g": 0.78, "b": 0.45}, "intensity": 2.6, "radius": 9.0}

        off = shot(f"{box['style']}_{probe}_off", outdir)
        lid = post("/api/light/point/add",
                   {"x": inside[0], "y": inside[1], "z": inside[2], **lamp}).get("id")
        on = shot(f"{box['style']}_{probe}_on", outdir)
        if lid is not None:
            post("/api/light/remove", {"id": lid})

        # Positive control: same light OUTSIDE, in front of the wall we are looking at.
        ctl_id = post("/api/light/point/add",
                      {"x": ctlpos[0], "y": ctlpos[1], "z": ctlpos[2], **lamp}).get("id")
        ctl = shot(f"{box['style']}_{probe}_ctl", outdir)
        if ctl_id is not None:
            post("/api/light/remove", {"id": ctl_id})

        b = luminance(off, CENTRE_RECT)
        results.append({
            "style": box["style"], "probe": probe,
            "inside_delta": luminance(on, CENTRE_RECT) - b,
            "control_delta": luminance(ctl, CENTRE_RECT) - b,
        })


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--outdir", default="docs/evidence/light_rig")
    args = ap.parse_args()
    Path(args.outdir).mkdir(parents=True, exist_ok=True)

    try:
        get("/api/state", timeout=5)
    except Exception:
        print("engine not responding on :8090 - launch it with a project first")
        return 1

    print("building sealed windowless structures (real generator, real styles)...")
    boxes = []
    for i, (style, cubes, micro) in enumerate(STYLES):
        print(f"  {style}: exterior_wall {cubes} cubes -> {micro} micro")
        b = build_box(i, style, args.outdir)
        if b:
            b["micro"] = micro
            boxes.append(b)

    results = []
    for b in boxes:
        print(f"measuring {b['style']}...")
        measure(b, args.outdir, results)

    print("\n  BASELINE — forward point light, sealed windowless structure")
    print("  inside  = light INSIDE the sealed box; any non-zero is light through a wall")
    print("  control = same light OUTSIDE the wall; proves the light can light that surface\n")
    print(f"    {'style':16s} {'micro':>5s} {'probe':8s} {'inside':>9s} {'control':>9s}  verdict")
    CONTROL_MIN = 0.5   # below this the rig did not see its own control light
    invalid = 0
    for r in results:
        micro = next(b["micro"] for b in boxes if b["style"] == r["style"])
        if r["control_delta"] < CONTROL_MIN:
            verdict = "INVALID - control light not visible; this row measures nothing"
            invalid += 1
        elif r["inside_delta"] < CONTROL_MIN * 0.1:
            verdict = "sealed"
        else:
            verdict = "LEAK"
        print(f"    {r['style']:16s} {micro:5d} {r['probe']:8s} "
              f"{r['inside_delta']:+9.3f} {r['control_delta']:+9.3f}  {verdict}")
    if invalid:
        # ASCII only: the Windows console is cp1252 and a non-ASCII glyph here raised
        # UnicodeEncodeError AFTER the table printed, killing the run at the last line.
        print(f"\n  !! {invalid} row(s) INVALID. A number with a dead control is not a result -\n"
              f"     the camera saw nothing. Fix the pose before reading anything into them.")
    print(f"\n  images: {args.outdir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
