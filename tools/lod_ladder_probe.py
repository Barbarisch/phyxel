#!/usr/bin/env python3
"""LOD ladder probe — measure WHERE each LOD tier actually switches, from the running engine.

WHY THIS EXISTS (2026-08-05, user): "we need to be able to test as we implement — measure
exactly where LOD kicks in and what detail level is active." Header defaults lie (several
thresholds are overwritten per frame from streaming config), and screenshots can't tell WHICH
level a tree/structure is at. This harness reads the engine's own selection state:

  GET  /api/debug/lod_report  — live per-tier thresholds + active-level histograms +
                                per-structure proxy state (readiness / minFade / level)
  POST /api/debug/lod_probe   — {x,y,z}: each tier's selected level at the current camera

Modes:
  python tools/lod_ladder_probe.py --report            # one snapshot, pretty-printed
  python tools/lod_ladder_probe.py --target X Y Z      # walk the camera away from a target,
                                                       #   record measured switch distances
  python tools/lod_ladder_probe.py --watch 2           # poll report every 2s (live tuning)
  ... --out docs/evidence/lod_ladder                   # archive raw JSONL + summary

The --target walk records, per rung: probe distance, per-tier level from lod_probe, and the
report's mesh_draws_by_level / card_draws / structures entries, then prints the MEASURED
distance band of every level transition it saw (the number to cite in docs/LodTierLedger.md,
never the header constant).
"""

import argparse
import json
import math
import os
import sys
import time
import urllib.error
import urllib.request

DEFAULT_URL = os.environ.get("PHYXEL_API_URL", "http://localhost:8090")

# Default camera ladder: dense where tiers hand off (fade band + early mesh levels),
# sparse in the tail. Override with --ladder.
LADDER = [40, 80, 120, 160, 200, 224, 240, 256, 272, 288, 304, 320, 336, 352, 380,
          420, 470, 520, 580, 650, 730, 820, 920, 1030, 1150, 1300, 1450, 1620, 1800, 2000]


def req(url, path, payload=None, timeout=25):
    full = url + path
    data = json.dumps(payload).encode() if payload is not None else None
    r = urllib.request.Request(
        full, data=data,
        headers={"Content-Type": "application/json"} if data else {},
        method="POST" if data else "GET")
    try:
        with urllib.request.urlopen(r, timeout=timeout) as resp:
            body = resp.read().decode()
        return json.loads(body) if body.strip() else {}
    except (urllib.error.URLError, TimeoutError, json.JSONDecodeError, OSError) as e:
        print(f"  !! {path}: {e}", file=sys.stderr)
        return None


def set_camera(url, x, y, z, yaw, pitch):
    # Nested position + mode free — the flat-args form is silently ignored (/api/camera).
    return req(url, "/api/camera", {
        "mode": "free",
        "position": {"x": float(x), "y": float(y), "z": float(z)},
        "yaw": float(yaw), "pitch": float(pitch)})


def get_camera(url):
    return req(url, "/api/camera")


def pretty_report(rep):
    if not rep:
        print("no report"); return
    cam = rep.get("camera", {})
    print(f"camera ({cam.get('x', 0):.0f}, {cam.get('y', 0):.0f}, {cam.get('z', 0):.0f})  "
          f"view_scale {rep.get('lod_view_scale', 1.0)}")
    res = rep.get("residency", {})
    print(f"residency  load {res.get('load_distance')}  unload {res.get('unload_distance')}")
    cl = rep.get("chunk_lod", {})
    print(f"chunk_lod  enabled={cl.get('enabled')}  target_px={cl.get('target_pixels')}  "
          f"by_level={cl.get('resident_by_level')}")
    fl = rep.get("far_lod_chunks", {})
    print(f"far_lod_chunks  enabled={fl.get('enabled')}  chunks={fl.get('chunks')}  "
          f"instances={fl.get('instances')}")
    ft = rep.get("far_terrain", {})
    print(f"far_terrain  enabled={ft.get('enabled')}  max={ft.get('max_distance')}  "
          f"resident={ft.get('tiles_resident')}  drawn={ft.get('tiles_drawn')}")
    tr = rep.get("far_trees", {})
    print(f"far_trees  fade_near={tr.get('fade_near')}  ladder={tr.get('mesh_level_dist')}  "
          f"band_end={tr.get('band_end')}")
    print(f"           mesh_draws_by_level={tr.get('mesh_draws_by_level')}  "
          f"card_draws={tr.get('card_draws')}  instances={tr.get('instances')}")
    st = rep.get("structures", {})
    print(f"structures  ladder={st.get('level_dist')}  "
          f"solid_proxies_in_band={st.get('solid_proxies_in_band')}")
    for e in st.get("entries", []):
        print(f"    {e['uuid'][:8]}  state={e['state']}  level={e['last_level']}  "
              f"dist={e['last_dist']:.0f}  readiness={e['readiness']:.2f}  "
              f"minFade={e['last_min_fade']:.2f}")
    print(f"grass {rep.get('grass')}   foliage {rep.get('foliage')}")
    print(f"characters {rep.get('characters')}   shadow {rep.get('shadow')}")


def walk(url, target, ladder, yaw_pitch, settle, out):
    tx, ty, tz = target
    rows = []
    prev_levels = {}
    transitions = []
    for dist in ladder:
        # Camera backs away along -Z from the target, looking at it, slightly elevated.
        cx, cy, cz = tx, ty + max(6.0, dist * 0.12), tz + dist
        pitch = -math.degrees(math.atan2(cy - ty, dist))
        set_camera(url, cx, cy, cz, -90.0, pitch)
        time.sleep(settle)
        probe = req(url, "/api/debug/lod_probe", {"x": tx, "y": ty, "z": tz}) or {}
        rep = req(url, "/api/debug/lod_report") or {}
        cam = get_camera(url) or {}
        row = {"rung": dist, "probe": probe, "camera": cam,
               "far_trees": rep.get("far_trees"), "structures": rep.get("structures"),
               "chunk_lod": rep.get("chunk_lod", {}).get("resident_by_level")}
        rows.append(row)
        # Record level transitions per tier as measured
        levels = {
            "tree_rep": (probe.get("tree_tier") or {}).get("representation"),
            "tree_mesh_level": (probe.get("tree_tier") or {}).get("mesh_level"),
            "structure_level": probe.get("structure_level"),
            "chunk_lod_level": probe.get("chunk_lod_level"),
            "character_lod": probe.get("character_lod"),
            "grass": probe.get("grass_active"),
            "foliage": probe.get("foliage_active"),
            "shadow": probe.get("shadow_reach"),
            "resident_meshed": (probe.get("resident_chunk") or {}).get("meshed"),
        }
        for k, v in levels.items():
            if k in prev_levels and prev_levels[k] != v and v is not None:
                transitions.append({"tier": k, "from": prev_levels[k], "to": v,
                                    "between": [prev_levels.get("_dist"), dist]})
            prev_levels[k] = v
        prev_levels["_dist"] = dist
        d = probe.get("distance", float("nan"))
        print(f"rung {dist:5d}  probe_dist {d:7.1f}  "
              f"tree={levels['tree_rep']}/{levels['tree_mesh_level']}  "
              f"struct=L{levels['structure_level']}  chunkLod={levels['chunk_lod_level']}  "
              f"grass={levels['grass']}  foliage={levels['foliage']}  "
              f"shadow={levels['shadow']}  meshed={levels['resident_meshed']}")

    print("\nMEASURED TRANSITIONS (cite these, not header constants):")
    for t in transitions:
        print(f"  {t['tier']}: {t['from']} -> {t['to']}  "
              f"between {t['between'][0]}u and {t['between'][1]}u")

    if out:
        os.makedirs(out, exist_ok=True)
        stamp = time.strftime("%Y%m%d_%H%M%S")
        raw = os.path.join(out, f"lod_ladder_{stamp}.jsonl")
        with open(raw, "w", encoding="utf-8") as f:
            for row in rows:
                f.write(json.dumps(row) + "\n")
            f.write(json.dumps({"transitions": transitions}) + "\n")
        print(f"\narchived: {raw}")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--url", default=DEFAULT_URL)
    ap.add_argument("--report", action="store_true", help="print one lod_report snapshot")
    ap.add_argument("--watch", type=float, metavar="SEC",
                    help="poll the report every SEC seconds until Ctrl+C")
    ap.add_argument("--target", nargs=3, type=float, metavar=("X", "Y", "Z"),
                    help="walk the camera away from this world position")
    ap.add_argument("--ladder", type=str,
                    help="comma-separated distances overriding the default ladder")
    ap.add_argument("--settle", type=float, default=1.5,
                    help="seconds to wait after each camera move (default 1.5)")
    ap.add_argument("--out", type=str, help="archive dir (e.g. docs/evidence/lod_ladder)")
    args = ap.parse_args()

    if args.report or (not args.watch and not args.target):
        pretty_report(req(args.url, "/api/debug/lod_report"))
        return
    if args.watch:
        try:
            while True:
                os.system("cls" if os.name == "nt" else "clear")
                pretty_report(req(args.url, "/api/debug/lod_report"))
                time.sleep(args.watch)
        except KeyboardInterrupt:
            return
    if args.target:
        ladder = ([int(x) for x in args.ladder.split(",")] if args.ladder else LADDER)
        walk(args.url, args.target, ladder, (-90.0, -10.0), args.settle, args.out)


if __name__ == "__main__":
    main()
