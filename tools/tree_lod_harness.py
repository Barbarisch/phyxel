#!/usr/bin/env python3
"""Tree-LOD ladder harness — photograph ONE planned tree at a ladder of view distances.

WHY THIS EXISTS. The far-tree tiers were being judged by flying around ProvingGrounds and
eyeballing a forest, which is exactly how the shrink/correspondence defects survived: in a
dense forest you cannot tell WHICH low-detail tree is supposed to be WHICH high-detail tree.
This harness isolates a single deterministic tree in a flat lab world (TreeLodLab) and walks
the camera outward from it on a fixed ladder, capturing a labeled screenshot + engine
counters per rung. The tree must keep its position, height, silhouette and colors at every
rung; transitions must be dithered dissolves, never size changes.

The target tree comes from /api/debug (command `flora_plan`) — the SAME deterministic
planFlora that near chunk stamping and the far tiers consume — so the harness is aimed at a
real generator tree, never a hand-placed lookalike (which the far tiers would not render).

Usage (engine already running the TreeLodLab project):

  python tools/tree_lod_harness.py                       # full ladder, auto-pick a tree
  python tools/tree_lod_harness.py --species oak         # prefer a template name substring
  python tools/tree_lod_harness.py --band                # fine steps through the fade band
  python tools/tree_lod_harness.py --out docs/evidence/tree_ladder

Every number printed is read back from the engine (camera pose, render counters); the
screenshots are copied out of the engine's capture path into --out with rung labels.
"""

import argparse
import json
import math
import os
import shutil
import sys
import time
import urllib.error
import urllib.request

DEFAULT_URL = "http://localhost:8090"

# The ladder. Bands (with default loadRadius 7 => loadDistance 224):
#   < ~202       resident chunks only (real stamped voxels)
#   ~202..254    fade band (dithered handoff, both tiers may draw)
#   < 450        instanced mesh level L2   (~500 cells on the oak)
#   < 650        instanced mesh level L3   (~180 cells)
#   < 900        instanced mesh level L4   (~80 cells)
#   >= 900       procedural cards
LADDER = [40, 120, 180, 228, 320, 450, 600, 800, 1100, 1500]

# --band mode: fine steps bracketing the fade band so the dissolve is captured mid-flight.
BAND_STEPS = [190, 200, 210, 220, 230, 240, 250, 260, 280]


def _req(url, path, payload=None, timeout=20):
    full = url + path
    data = json.dumps(payload).encode() if payload is not None else None
    req = urllib.request.Request(
        full, data=data,
        headers={"Content-Type": "application/json"} if data else {},
        method="POST" if data else "GET")
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            body = r.read().decode()
        return json.loads(body) if body.strip() else {}
    except (urllib.error.URLError, TimeoutError, json.JSONDecodeError, OSError):
        return None


def wait_ready(url, tries=40):
    for _ in range(tries):
        if _req(url, "/api/state") is not None:
            return True
        time.sleep(3)
    return False


def set_camera(url, x, y, z, yaw, pitch):
    # Raw POST with nested position + mode free — the flat-args form is silently ignored.
    return _req(url, "/api/camera", {
        "mode": "free",
        "position": {"x": float(x), "y": float(y), "z": float(z)},
        "yaw": float(yaw), "pitch": float(pitch),
    })


def flora_plan(url, x, z, radius):
    return _req(url, "/api/debug/flora_plan",
                {"x": int(x), "z": int(z), "radius": int(radius)})


def pick_tree(url, species_filter, search_radius):
    """The most ISOLATED planned tree near the origin (largest nearest-neighbor distance),
    so neighboring silhouettes don't contaminate the ladder shots. Optionally constrained
    to templates whose name contains --species."""
    plan = flora_plan(url, 0, 0, search_radius)
    if not plan or not plan.get("trees"):
        return None, []
    trees = plan["trees"]
    # Bushes/ferns have no far representation — only rungs from actual trees are meaningful.
    def is_tree(t):
        n = t["template"].lower()
        return not any(k in n for k in ("bush", "fern", "shrub", "flower", "grass"))
    candidates = [t for t in trees if is_tree(t)]
    if species_filter:
        filtered = [t for t in candidates if species_filter.lower() in t["template"].lower()]
        if filtered:
            candidates = filtered
    if not candidates:
        return None, trees

    def nn_dist(t):
        best = float("inf")
        for o in trees:
            if o is t:
                continue
            d = math.hypot(o["x"] - t["x"], o["z"] - t["z"])
            best = min(best, d)
        return best

    best = max(candidates, key=nn_dist)
    return best, trees


def screenshot(url):
    r = _req(url, "/api/screenshot")   # GET, not POST
    return (r or {}).get("path")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--url", default=DEFAULT_URL)
    ap.add_argument("--species", default=None, help="template-name substring to prefer (e.g. oak)")
    ap.add_argument("--search-radius", type=int, default=256)
    ap.add_argument("--band", action="store_true",
                    help="fine steps through the near-fade band instead of the full ladder")
    ap.add_argument("--azimuth", type=float, default=180.0,
                    help="degrees; direction FROM the tree TO the camera (180 = camera south of tree)")
    ap.add_argument("--settle", type=float, default=8.0,
                    help="seconds per rung for streaming/far tiles/species meshes to catch up")
    ap.add_argument("--out", default="docs/evidence/tree_ladder")
    args = ap.parse_args()

    if not wait_ready(args.url):
        print(f"engine not responding at {args.url} — launch TreeLodLab first:\n"
              f"  phyxel.exe --project <Documents>\\PhyxelProjects\\TreeLodLab", file=sys.stderr)
        return 2

    tree, all_trees = pick_tree(args.url, args.species, args.search_radius)
    if not tree:
        print(f"flora_plan returned no trees within {args.search_radius}u of origin — "
              "wrong world, or the generator plans no flora here.", file=sys.stderr)
        return 2

    tx, ty, tz = tree["x"], tree["y"], tree["z"]
    print(f"target tree: {tree['template']} at ({tx}, {ty}, {tz}) "
          f"[{len(all_trees)} planned within {args.search_radius}u]")

    os.makedirs(args.out, exist_ok=True)
    az = math.radians(args.azimuth)
    dirx, dirz = math.sin(az), math.cos(az)   # unit vector tree -> camera on the ground plane

    rungs = BAND_STEPS if args.band else LADDER
    rows = []
    for dist in rungs:
        # Camera on the azimuth ray, lifted a little with distance so the whole crown stays in
        # frame; aimed at the tree's mid-height so the framing is consistent across rungs.
        cx = tx + dirx * dist
        cz = tz + dirz * dist
        cy = ty + max(6.0, dist * 0.10)
        aim_y = ty + 8.0                       # ~mid-crown for typical templates
        dx, dy, dzz = tx - cx, aim_y - cy, tz - cz
        yaw = math.degrees(math.atan2(dzz, dx))            # editor free-cam: 0 = +X, -90 = -Z
        pitch = math.degrees(math.atan2(dy, math.hypot(dx, dzz)))

        set_camera(args.url, cx, cy, cz, yaw, pitch)
        time.sleep(args.settle)

        cam = _req(args.url, "/api/camera") or {}
        rs = _req(args.url, "/api/render/stats") or {}
        row = {
            "distance": dist,
            "pose_readback": cam.get("position", {}),
            "visible_chunks": rs.get("visible_chunk_count"),
            "far_tiles_drawn": rs.get("far_tiles_drawn"),
            "far_triangles": rs.get("far_triangles"),
        }
        shot = screenshot(args.url)
        if shot and os.path.exists(shot):
            label = f"d{dist:04d}_{tree['template']}.png"
            dest = os.path.join(args.out, label)
            shutil.copyfile(shot, dest)
            row["screenshot"] = dest
        else:
            row["screenshot"] = None
        rows.append(row)
        print(f"  d={dist:>5}  chunks={row['visible_chunks']}  farTris={row['far_triangles']}"
              f"  -> {row['screenshot']}")

    manifest = {"tree": tree, "azimuth": args.azimuth, "rungs": rows}
    mpath = os.path.join(args.out, "ladder.json")
    with open(mpath, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=1)
    print(f"\nwrote {len(rows)} rungs -> {mpath}")
    print("Judge the shots pairwise: position/height/silhouette/colors must match across every "
          "adjacent pair; any rung where the tree is smaller than the previous FARTHER rung is a "
          "scaling defect, and any visible size-shrink near the resident band is the old bug.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
