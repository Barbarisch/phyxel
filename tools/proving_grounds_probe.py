#!/usr/bin/env python3
"""ProvingGrounds — survey + measurement harness.

WHY THIS EXISTS. A showcase world you only look at is a demo. This turns it into a REGRESSION
FIXTURE: fixed poses, machine-read numbers, one comparable row per run. Every LOD / transition /
render change on the roadmap is supposed to be judged against it, so the numbers have to come from
the engine's own counters rather than from an opinion about a screenshot.

Two modes:

  --survey    Probe the generated terrain to FIND good vantages (highest ground for a mountain
              look, water surfaces, flat open ground, dense canopy). Prints a `vantages` block to
              paste into game.json. Run this once per seed — hardcoding camera coordinates against
              terrain nobody has measured is how you end up filming the inside of a hill.

  --measure   Drive each pinned vantage and record engine counters + a screenshot per pose.
              Writes a table to stdout and JSONL for archiving under docs/evidence/.

Both modes talk to a RUNNING engine over the HTTP API. They never place voxels, never fabricate
terrain, and never invent numbers: every value printed is read back from /api/render_stats,
/api/debug/engine_timing or /api/camera.

  python tools/proving_grounds_probe.py --survey
  python tools/proving_grounds_probe.py --measure --out docs/evidence/pg_baseline.jsonl
"""

import argparse
import json
import sys
import time
import urllib.error
import urllib.request

DEFAULT_URL = "http://localhost:8090"


# ---------------------------------------------------------------------------
# transport
# ---------------------------------------------------------------------------

def _req(url, path, payload=None, timeout=20):
    """GET when payload is None, else POST. Returns parsed JSON or None."""
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


def wait_ready(url, tries=60):
    for _ in range(tries):
        if _req(url, "/api/state") is not None:
            return True
        time.sleep(3)
    return False


def terrain_height(url, x, z):
    r = _req(url, f"/api/terrain/height?x={int(x)}&z={int(z)}") or {}
    if r.get("surface_y") is None:
        r = _req(url, "/api/terrain/height", {"x": int(x), "z": int(z)}) or {}
    return r.get("surface_y")


def set_camera(url, x, y, z, yaw, pitch):
    # NOTE: the MCP set_camera tool takes FLAT args and silently drops a `position` array; the raw
    # POST below is the reliable path, and only Free mode syncs position. (Water-session finding.)
    return _req(url, "/api/camera", {
        "mode": "free",
        "position": {"x": float(x), "y": float(y), "z": float(z)},
        "yaw": float(yaw), "pitch": float(pitch),
    })


def render_stats(url):
    # NOTE the path: it is /api/render/stats, NOT /api/render_stats. The wrong one returns an empty
    # body, which json-decodes to nothing and quietly fills the whole report with None — a table of
    # Nones looks like "the engine has no geometry" rather than "you used the wrong URL".
    return _req(url, "/api/render/stats") or {}


def engine_timing(url):
    return _req(url, "/api/debug/engine_timing") or {}


def far_lod_stats(url):
    """far_chunks / far_instances -- the C3.3 far-CHUNK tier (persisted LOD pyramids).

    These are NOT in /api/render/stats, which only carries far TERRAIN counters. The two tiers are
    different things: far terrain is the generator's heightmap (cannot show a building), far-LOD
    chunks are saved structures/edits drawn from chunk_lod_blobs. Reporting only the terrain
    numbers would hide a completely dead structure tier.

    Posting with no toggle keys is a read: the handler echoes the counters without changing state.
    """
    return _req(url, "/api/debug/far_lod", {}) or {}


# ---------------------------------------------------------------------------
# survey — find vantages against the REAL generated terrain
# ---------------------------------------------------------------------------

def survey(url, radius, step):
    """Sample a grid of surface heights and classify the terrain we actually got.

    Streaming worlds only have chunks near the camera, so this WALKS the camera across the grid
    and lets terrain stream in before sampling. That is slow and unavoidable: probing without
    moving returns None for everything outside the current residency sphere, which reads as
    'the world is empty' rather than 'you did not look'.
    """
    samples = []
    coords = [(x, z) for x in range(-radius, radius + 1, step)
                     for z in range(-radius, radius + 1, step)]
    print(f"surveying {len(coords)} columns (streaming — this walks the camera, be patient)",
          file=sys.stderr)

    for i, (x, z) in enumerate(coords):
        # Park the camera above the sample point so the chunk streams in, then read.
        set_camera(url, x, 400, z, -90, -89)
        time.sleep(1.2)
        y = terrain_height(url, x, z)
        if y is not None:
            samples.append({"x": x, "z": z, "surface_y": y})
        if (i + 1) % 10 == 0:
            print(f"  {i+1}/{len(coords)} ({len(samples)} hits)", file=sys.stderr)

    if not samples:
        print("NO TERRAIN FOUND — the world generated nothing, or streaming never caught up.",
              file=sys.stderr)
        return samples

    ys = sorted(s["surface_y"] for s in samples)
    lo, hi = ys[0], ys[-1]
    med = ys[len(ys) // 2]
    print(f"\nsurface_y: min {lo}  median {med}  max {hi}  (n={len(ys)})", file=sys.stderr)
    print("water.seaLevel should sit between the median and the low end so there is dry land, "
          "a shoreline AND basins to fill. Re-measure if you change the seed.", file=sys.stderr)
    return samples


# ---------------------------------------------------------------------------
# measure — drive the pinned vantages
# ---------------------------------------------------------------------------

def measure(url, vantages, settle, out_path):
    rows = []
    for v in vantages:
        set_camera(url, v["x"], v["y"], v["z"], v["yaw"], v["pitch"])
        time.sleep(settle)   # let streaming + far-tile builds catch up before reading counters

        rs, et, fl = render_stats(url), engine_timing(url), far_lod_stats(url)
        cam = _req(url, "/api/camera") or {}
        pos = cam.get("position", {})

        row = {
            "vantage": v["name"],
            # Pose READ BACK from the engine, not the pose we asked for. A silently-rejected
            # camera move is the classic way to compare two measurements of the same frame.
            "pose": {"x": pos.get("x"), "y": pos.get("y"), "z": pos.get("z"),
                     "yaw": cam.get("yaw"), "pitch": cam.get("pitch")},
            "visible_chunks": rs.get("visible_chunk_count"),
            "visible_faces": rs.get("total_visible_faces"),
            "far_tiles_drawn": rs.get("far_tiles_drawn"),
            "far_tiles_resident": rs.get("far_tiles_resident"),
            "far_triangles": rs.get("far_triangles"),
            # C3.3 far-CHUNK tier (saved structures drawn from persisted pyramids) -- a DIFFERENT
            # tier from far terrain above, and the only one that can show a building at range.
            "far_chunks": fl.get("far_chunks"),
            "far_instances": fl.get("far_instances"),
            "cpu_frame_ms": round(et.get("cpuFrameTime", 0.0), 3),
        }
        rows.append(row)

        shot = _req(url, "/api/screenshot")   # GET, not POST
        if shot and shot.get("path"):
            row["screenshot"] = shot["path"]

    hdr = (f"{'vantage':<22}{'chunks':>8}{'faces':>10}{'farTiles':>10}{'farTris':>10}"
           f"{'farChunk':>9}{'farInst':>9}{'cpu_ms':>9}")
    print(hdr)
    print("-" * len(hdr))
    for r in rows:
        print(f"{r['vantage']:<22}{str(r['visible_chunks']):>8}{str(r['visible_faces']):>10}"
              f"{str(r['far_tiles_drawn']):>10}{str(r['far_triangles']):>10}"
              f"{str(r['far_chunks']):>9}{str(r['far_instances']):>9}"
              f"{r['cpu_frame_ms']:>9}")

    # ⚠️ cpu_frame_ms is NOT the presented frame rate. This project documents +/-20% restart
    # variance on the FPS counter and the status bar is the real presented rate; treat this column
    # as a coarse regression signal and read FPS off the screenshots for anything load-bearing.
    print("\nNOTE: cpu_frame_ms is a coarse CPU-side signal, not the presented frame rate. "
          "Read FPS from the status bar in the screenshots for any claim that matters.")

    if out_path:
        with open(out_path, "w", encoding="utf-8") as f:
            for r in rows:
                f.write(json.dumps(r) + "\n")
        print(f"\nwrote {len(rows)} rows -> {out_path}")
    return rows


# ---------------------------------------------------------------------------
# annuli — the WRv2 M2 deadzone metric, as a number (plan §6)
# ---------------------------------------------------------------------------

def annuli(url):
    """Tree instances drawn per 50u camera-distance annulus at the CURRENT pose, per tier,
    with the §6 continuity verdict: inside the far-tree band no annulus may fall below 60%
    of the mean of its two neighbors. A deadzone shows as a collapsed bucket — a number,
    not a squint at a screenshot. (Near-field stamped trees are not counted here; the band
    check therefore starts past the residency edge where the far tiers own trees.)"""
    rs = render_stats(url)
    mesh = rs.get("far_tree_mesh_annuli") or []
    card = rs.get("far_tree_card_annuli") or []
    if not mesh and not card:
        print("engine reports no annuli counters — build too old, or far terrain disabled",
              file=sys.stderr)
        return 2

    total = [m + c for m, c in zip(mesh, card)]
    print(f"{'annulus':>12} {'mesh':>8} {'cards':>8} {'total':>8}")
    for i, (m, c, t) in enumerate(zip(mesh, card, total)):
        print(f"{i*50:>5}-{(i+1)*50:<5} {m:>8} {c:>8} {t:>8}")

    # Continuity: check inside the populated band only (first..last nonzero annulus), and
    # only from 250u out (inside that, resident chunks own trees and the far count is
    # legitimately ramping in across the fade band).
    nz = [i for i, t in enumerate(total) if t > 0]
    verdict = "PASS"
    if nz:
        lo = max(min(nz), 5)             # 250u
        hi = max(nz)
        for i in range(lo + 1, hi):
            neigh = (total[i - 1] + total[i + 1]) / 2.0
            if neigh >= 40 and total[i] < 0.6 * neigh:   # tiny neighbors = no forest, skip
                print(f"  !! annulus {i*50}-{(i+1)*50} collapsed: {total[i]} vs "
                      f"neighbor mean {neigh:.0f}")
                verdict = "FAIL"
    print(f"deadzone continuity: {verdict}")
    return 0 if verdict == "PASS" else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--url", default=DEFAULT_URL)
    ap.add_argument("--survey", action="store_true", help="probe terrain to find vantages")
    ap.add_argument("--measure", action="store_true", help="drive the pinned vantages")
    ap.add_argument("--annuli", action="store_true",
                    help="per-annulus tree-instance histogram + deadzone verdict (current pose)")
    ap.add_argument("--game-json", default=None, help="game.json holding testVantages")
    ap.add_argument("--radius", type=int, default=600, help="survey half-extent (world units)")
    ap.add_argument("--step", type=int, default=150, help="survey grid step")
    ap.add_argument("--settle", type=float, default=12.0, help="seconds to settle per vantage")
    ap.add_argument("--out", default=None, help="JSONL output path for --measure")
    args = ap.parse_args()

    if not wait_ready(args.url):
        print(f"engine not responding at {args.url}", file=sys.stderr)
        return 2

    if args.annuli:
        return annuli(args.url)

    if args.survey:
        s = survey(args.url, args.radius, args.step)
        print(json.dumps(s, indent=1))
        return 0

    if args.measure:
        if not args.game_json:
            print("--measure needs --game-json", file=sys.stderr)
            return 2
        with open(args.game_json, encoding="utf-8") as f:
            gj = json.load(f)
        vs = gj.get("testVantages", {}).get("vantages", [])
        if not vs:
            print("no vantages pinned in game.json — run --survey first", file=sys.stderr)
            return 2
        measure(args.url, vs, args.settle, args.out)
        return 0

    ap.print_help()
    return 1


if __name__ == "__main__":
    sys.exit(main())
