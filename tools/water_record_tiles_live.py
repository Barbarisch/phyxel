"""§27 W1b live (docs/WaterCore.md 27.12): a walled 46 x 28 m pond 1 m deep on the bench with tile recording ON, filmed close.
Timeline: placed still - its 8 m tiles record (the record draws them, W1a); at --push-at s a push (water_av_impulse) - the tiles
it touches wake first, its waves wake the tiles ahead of them; then it settles and the tiles record again. Polled every 0.25 s:
tiles in the record, wakes, the last record / wake call (ms, main thread), the water's GPU ms, and the books (cells + record,
read from the volume and the chunk spans). Prediction written before the run: no blank frame and no visible jump when a tile
records or wakes; the books flat to 1e-3 m^3; a record / wake call under ~10 ms (a tile on the host, 27.12).

    python tools/water_record_tiles_live.py [--seconds 45] [--push-at 10] [--strength 4] [--tag w1b_live]
Writes docs/evidence/water_core_h1/<tag>.{json,mp4}. The bench engine (WaterBench_Creek, 8114) must be running, the GPU idle.
"""
import argparse, json, pathlib, shutil, subprocess, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set, world_job

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_h1"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8114")
    ap.add_argument("--seconds", type=float, default=45.0)
    ap.add_argument("--push-at", type=float, default=10.0)
    ap.add_argument("--strength", type=float, default=4.0)
    ap.add_argument("--after-s", type=float, default=2.0)
    ap.add_argument("--tag", default="w1b_live")
    ap.add_argument("--no-video", action="store_true")
    ap.add_argument("--record-off", action="store_true", help="the control: tile recording OFF (the pond only freezes)")
    ap.add_argument("--census-every", type=int, default=8, help="polls between exact censuses; 0 = only the first and the last. A census "
                    "collects the GPU call between two steps, which keeps columns 'moving' (27.13 open item 1) - 0 for a clip")
    ap.add_argument("--quiet-scale", type=float, default=None, help="the per-column freeze thresholds x this (26.34; default 10, 27.24)")
    args = ap.parse_args()
    api = Api(args.url); api.wait_status(300)
    api.debug("water_av_realtime", {"on": True})
    if args.quiet_scale is not None: print("freeze test:", api.debug("water_av_partial_wake", {"quiet_scale": args.quiet_scale, "rest_ticks": 0}))
    l, w, x0, z0 = 46, 28, 2, 0
    for v in api.debug("water_av_list").get("volumes", []): api.debug("water_av_destroy", {"id": v["id"], "discard": True})
    # the kerb (StoneBricks, 2 high) - the pond held by the world, not by the volume's box edge (27.11's rig lesson)
    for b in ((x0 - 1, z0 - 1, x0 - 1, z0 + w), (x0 + l, z0 - 1, x0 + l, z0 + w), (x0 - 1, z0 - 1, x0 + l, z0 - 1), (x0 - 1, z0 + w, x0 + l, z0 + w)):
        world_job(api, "/api/world/fill", {"x1": b[0], "y1": 16, "z1": b[1], "x2": b[2], "y2": 17, "z2": b[3]}, material="StoneBricks")
    print("kerb (read back):", api.get(f"/api/world/voxel?x={x0 + l}&y=17&z=5").get("material"))
    box = {"x1": x0, "y1": 16, "z1": z0, "x2": x0 + l - 1, "y2": 17, "z2": z0 + w - 1}
    api.debug("water_av_record_draw", {"on": True, "radius": 64})
    camera_set(api, {"x": 14.0, "y": 21.0, "z": 33.0, "yaw": -120.0, "pitch": -24.0})
    vid = api.debug("water_av_create", {**box, "cellSize": 1 / 9, "backend": "gpu", "auto_sleep": False})["volume"]["id"]
    print("recording:", api.debug("water_av_record_tiles", {"id": vid, "on": not args.record_off, "after_s": args.after_s}))
    ff = None
    if not args.no_video:
        ff = subprocess.Popen([shutil.which("ffmpeg"), "-y", "-loglevel", "error", "-f", "gdigrab", "-framerate", "30", "-i", "title=WaterBench_Creek",
                               "-t", str(args.seconds), "-vf", "crop=954:560:243:60", "-c:v", "libx264", "-pix_fmt", "yuv420p", str(EV / f"{args.tag}.mp4")])
    t0 = time.time()
    api.debug("place_water_box", {"target": "core", **{**box, "y2": 16}, "mass": 1.0})
    rows, pushed = [], False
    while time.time() - t0 < args.seconds:
        t = time.time() - t0
        if not pushed and t >= args.push_at:
            api.debug("water_av_impulse", {"x": x0 + 10.0, "y": 16.9, "z": z0 + w - 8.0, "radius": 0.5, "strength": args.strength, "dy": -1.0})
            pushed = True
        lst = api.debug("water_av_list")
        v = next((x for x in lst["volumes"] if x["id"] == vid), {})
        sp = api.debug("water_spans_stored", {"x1": x0, "z1": z0, "x2": x0 + l - 1, "z2": z0 + w - 1})
        # the books from an exact read every 2 s (water_av_list's total_mass is the once-a-second realtime mirror: it read 0 for a
        # second after a tile recorded - the first live run's "1.29e3 m^3 off" was that, not lost water). A census is a whole-volume
        # read-back (~70 ms): not every poll.
        last = time.time() - t0 + 0.6 >= args.seconds
        every = args.census_every if args.census_every > 0 else 10**9
        census = api.debug("water_av_census", {"id": vid}) if (len(rows) % every == 0 or last) else None
        rows.append({"t": round(t, 2), "recorded_tiles": v.get("recorded_tiles"), "tiles_recorded": v.get("tiles_recorded"), "tiles_woken": v.get("tiles_woken"), "woken_by_write": v.get("tiles_woken_by_write"), "wake_all": v.get("gpu_wake_all"),
                     "last_record_ms": v.get("last_record_ms"), "last_wake_ms": v.get("last_wake_ms"), "gpu_device_ms": v.get("gpu_device_ms"),
                     "ke": v.get("kinetic_energy"), "residual": v.get("rbgs_residual"), "awake_columns": v.get("awake_columns"), "frozen_columns": v.get("frozen_columns"), "bricks": v.get("gpu_bricks"),
                     "cells_m3": lst.get("total_mass"), "census_m3": census.get("mass_m3") if census else None, "census_record_m3": census.get("record_m3") if census else None,
                     "recorded_m3": v.get("recorded_m3"), "spans_m3": sp.get("total_depth"),
                     "seed_unplaced_m3": v.get("seed_unplaced_m3")})
        time.sleep(0.25)
    if ff: ff.wait()
    rd = api.debug("water_av_record_draw", {})
    # the books from ONE call (the census reads the cells and the record together - read apart, tiles recorded between the reads)
    exact = [r for r in rows if r["census_m3"] is not None and r["census_record_m3"] is not None and r["census_m3"] + r["census_record_m3"] > 1.0]
    placed = exact[0]["census_m3"] + exact[0]["census_record_m3"]
    books = [abs(r["census_m3"] + r["census_record_m3"] - placed) for r in exact]
    out = {"tag": args.tag, "placed_m3": placed, "books_worst_m3": max(books), "record_ms_max": max(r["last_record_ms"] or 0 for r in rows),
           "wake_ms_max": max(r["last_wake_ms"] or 0 for r in rows), "quiet_scale": args.quiet_scale, "record_jump_mm_max": v.get("record_jump_mm_max"), "record_spread_mm_max": v.get("record_spread_mm_max"), "record_draw": rd, "rows": rows}
    (EV / f"{args.tag}.json").write_text(json.dumps(out, indent=1))
    for r in rows[::8]:
        print(f"t {r['t']:5.1f}  in record {r['recorded_tiles']}  recorded {r['tiles_recorded']} woken {r['tiles_woken']} (by a write {r['woken_by_write']}, whole-volume wakes {r['wake_all']})  awake {r['awake_columns']} frozen {r['frozen_columns']} "
              f"bricks {r['bricks']}  gpu {r['gpu_device_ms']} ms  cells {r['cells_m3']:.3f} + spans {r['spans_m3']:.3f}")
    print(f"placed {placed:.4f} m3; books worst {out['books_worst_m3']:.2e} m3; record call max {out['record_ms_max']:.1f} ms, wake call max {out['wake_ms_max']:.1f} ms; record draw build max {rd.get('build_ms_max')} ms")
    print("wrote", EV / f"{args.tag}.json")


if __name__ == "__main__":
    main()
