"""WaterCore.md 27.15: holes in the drawn water while recorded tiles wake - measured every frame, not from a video.

The 27.12 rig (the walled 46 x 28 m pond, 1 m deep, recording ON, the record drawn), the camera as the clips'. When every tile
is in the record, a watch on each 8 m tile's centre starts (water_av_watch: per frame, what ANY field draws there and whether
the column is recorded), then the 27.12 push. A HOLE = a frame where no field draws water at a point of the pond.
Prints, per tile, the hole frames (first / last time after the push, count) and the recorded flag around them.

    python tools/water_wake_holes.py [--after 6] [--strength 4] [--json out.json]
Prediction for a correct hand-over: 0 hole frames anywhere. The bench engine (8114) must be running; it must start with an
EMPTY record (27.14 finding 3: a fresh engine).
"""
import argparse, json, pathlib, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set, world_job


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8114")
    ap.add_argument("--after", type=float, default=6.0, help="seconds watched after the push")
    ap.add_argument("--strength", type=float, default=4.0)
    ap.add_argument("--json", default=None)
    ap.add_argument("--edges", action="store_true", help="also watch 0.3 m either side of every tile boundary (100 points)")
    a = ap.parse_args()
    api = Api(a.url); api.wait_status(300)
    api.debug("water_av_realtime", {"on": True})
    l, w, x0, z0 = 46, 28, 2, 0
    for v in api.debug("water_av_list").get("volumes", []): api.debug("water_av_destroy", {"id": v["id"]})
    for b in ((x0 - 1, z0 - 1, x0 - 1, z0 + w), (x0 + l, z0 - 1, x0 + l, z0 + w), (x0 - 1, z0 - 1, x0 + l, z0 - 1), (x0 - 1, z0 + w, x0 + l, z0 + w)):
        world_job(api, "/api/world/fill", {"x1": b[0], "y1": 16, "z1": b[1], "x2": b[2], "y2": 17, "z2": b[3]}, material="StoneBricks")
    box = {"x1": x0, "y1": 16, "z1": z0, "x2": x0 + l - 1, "y2": 17, "z2": z0 + w - 1}
    api.debug("water_av_record_draw", {"on": True, "radius": 64})
    camera_set(api, {"x": 14.0, "y": 21.0, "z": 33.0, "yaw": -120.0, "pitch": -24.0})
    vid = api.debug("water_av_create", {**box, "cellSize": 1 / 9, "backend": "gpu", "auto_sleep": False})["volume"]["id"]
    api.debug("water_av_record_tiles", {"id": vid, "on": True, "after_s": 2.0})
    c0 = api.debug("water_av_census", {"id": vid})
    if c0.get("record_m3", 0) > 1e-3: sys.exit(f"the record is not empty ({c0.get('record_m3')} m^3) - restart the engine (27.14 finding 3)")
    api.debug("place_water_box", {"target": "core", **{**box, "y2": 16}, "mass": 1.0})
    t0 = time.time()
    while time.time() - t0 < 30:
        v = next(x for x in api.debug("water_av_list")["volumes"] if x["id"] == vid)
        if v.get("recorded_tiles") == 24: break
        time.sleep(0.25)
    print("tiles in the record before the push:", v.get("recorded_tiles"))
    time.sleep(1.0)
    tiles = [(tx, tz) for tz in range(4) for tx in range(6)]
    pts = [[tx * 8 + 4.05, min(tz * 8 + 4, 26) + 0.05] for tx, tz in tiles]
    names = [f"tile ({tx},{tz}) centre" for tx, tz in tiles]
    if a.edges:   # 0.3 m either side of every tile boundary inside the pond (a seam shows there first)
        for ex in (8, 16, 24, 32, 40):
            for tz in range(4):
                for d in (-0.3, 0.3): pts.append([ex + d, min(tz * 8 + 4, 26) + 0.05]); names.append(f"x edge {ex}{d:+.1f} z {min(tz * 8 + 4, 26)}")
        for ez in (8, 16, 24):
            for tx in range(6):
                for d in (-0.3, 0.3): pts.append([tx * 8 + 4.05, ez + d]); names.append(f"z edge {ez}{d:+.1f} x {tx * 8 + 4}")
    api.debug("water_av_watch", {"points": pts})
    time.sleep(0.5)
    tp = time.time()
    api.debug("water_av_impulse", {"x": x0 + 10.0, "y": 16.9, "z": z0 + w - 8.0, "radius": 0.5, "strength": a.strength, "dy": -1.0})
    tpush = None
    time.sleep(a.after)
    res = api.debug("water_av_watch", {"stop": True})
    rows = res["rows"]
    # the push's time on the watch clock: the watch started 0.5 s before it (the API round trip is a few ms)
    t_push = rows[0][0] + (tp - (tp - 0.5)) if rows else 0.0
    out = {"tiles": [], "frames": len(rows)}
    total = 0
    print(f"{len(rows)} frames watched; HOLE = no field draws water at the tile's centre")
    for k, nm in enumerate(names):
        holes = [r for r in rows if r[1 + 4 * k + 2] is None]
        rec = [r[1 + 4 * k + 3] for r in rows]
        flips = [round(rows[i][0] - t_push, 3) for i in range(1, len(rows)) if rec[i] != rec[i - 1]]
        total += len(holes)
        e = {"point_name": nm, "point": pts[k], "hole_frames": len(holes), "first_s": round(holes[0][0] - t_push, 3) if holes else None,
             "last_s": round(holes[-1][0] - t_push, 3) if holes else None, "recorded_flips_s": flips,
             "recorded_at_holes": sorted(set(r[1 + 4 * k + 3] for r in holes))}
        out["tiles"].append(e)
        if holes or k < len(tiles): print(f"  {nm:24s} x {pts[k][0]:5.2f} z {pts[k][1]:5.2f}: holes {len(holes):4d}  first {e['first_s']}  last {e['last_s']}  recorded at holes {e['recorded_at_holes']}  recorded flips {flips}")
    print(f"TOTAL hole frames: {total} (prediction for a correct hand-over: 0)")
    if a.json: pathlib.Path(a.json).write_text(json.dumps({**out, "rows": rows, "points": pts}))


if __name__ == "__main__":
    main()
