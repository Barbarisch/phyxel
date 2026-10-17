"""WaterCore.md 26.18 / 26.19 - dig under the running creek and measure what it costs and where the water goes.

With the pond creek running on the bench (tools/water_creek_bench.py --run --pond), clears the bed voxel (x, 15, z) under the
water - the gravel the carve put there, a pit down to y 15 m - one column at a time, every --every s. After each dig it logs
the record's re-read time (occ_refresh_ms = the box's occupancy re-sampled from the world, gpu_upload_ms = the water read back
and re-uploaded: both main-thread work in the frame that sees the dig), the frame time right after, and the books. At the end it
waits --settle s and reports the stored water against the pits' volume (the water must run into the holes; digging never
makes water: stored mass may only rise by what flowed in from the springs).

    python tools/water_creek_dig.py [--digs 20] [--every 1.0] [--x0 40] [--z 31] [--record 90]

26.19: the water now sees a dig by patching only the changed cells in place (gpu_patches) instead of a full re-read
(gpu_uploads); both count as "seen". Each dig also logs the clear command's own stage times (timing_ms), the re-read's cells,
and the slowest main-loop frame in the 240 frames after it (frame_pacing: its total and its largest stage).

Writes docs/evidence/water_core_h1/creekbench_dig<tag>.json (+ .mp4 when --record > 0).
"""
import argparse, json, pathlib, shutil, subprocess, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set, world_job
import water_creek_bench as B


def vol(api):
    return api.debug("water_av_list")["volumes"][0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8114")
    ap.add_argument("--digs", type=int, default=20)
    ap.add_argument("--every", type=float, default=1.0)
    ap.add_argument("--x0", type=int, default=40)
    ap.add_argument("--z", type=int, default=31)
    ap.add_argument("--step", type=int, default=2, help="x step between digs (1 = a continuous trench)")
    ap.add_argument("--settle", type=float, default=60.0)
    ap.add_argument("--record", type=float, default=0.0)
    ap.add_argument("--tag", default="")
    ap.add_argument("--cam", default="", help="x,y,z,yaw,pitch (default: beside the first dig, looking along it)")
    args = ap.parse_args()
    api = Api(args.url)
    v0 = vol(api)
    rec = None
    name = f"creekbench_dig{args.tag}"
    cam = [float(c) for c in args.cam.split(",")] if args.cam else [args.x0 - 4.0, 18.0, args.z + 4.5, -35.0, -28.0]
    camera_set(api, {"x": cam[0], "y": cam[1], "z": cam[2], "yaw": cam[3], "pitch": cam[4]})
    time.sleep(1.0)
    if args.record > 0 and shutil.which("ffmpeg"):
        rec = subprocess.Popen([shutil.which("ffmpeg"), "-y", "-loglevel", "error", "-f", "gdigrab", "-framerate", "30", "-t", str(args.record),
                                "-i", "title=WaterBench_Creek", "-vf", "crop=960:630:240:42", "-c:v", "libx264", "-crf", "28",
                                "-pix_fmt", "yuv420p", str(B.EV / f"{name}.mp4")])
        time.sleep(2.0)
    pit_m3 = 0.0
    log = []
    for k in range(args.digs):
        x = args.x0 + args.step * k
        pit_m3 += B.bed_top(x) - 15.0          # the gravel above y 15 m in this 1 m x 1 m column
        before = vol(api)
        t0 = time.time()
        seen = lambda w: w["gpu_uploads"] + w.get("gpu_patches", 0)
        cr = world_job(api, "/api/world/clear", {"x1": x, "y1": 15, "z1": args.z, "x2": x, "y2": 15, "z2": args.z})
        while seen(vol(api)) == seen(before) and time.time() - t0 < 5.0: time.sleep(0.05)   # the water saw it
        v = vol(api)
        et = api.get("/api/debug/engine_timing")
        fp = api.get("/api/debug/frame_pacing?frames=240").get("series", [])
        worst = max(fp, key=lambda f: f["values"].get("Frame", 0.0)) if fp else {"values": {}}
        wv = worst["values"]
        stages = sorted(((k, ms) for k, ms in wv.items() if k.count("/") >= 1 and k not in ("Frame/update", "Frame/update/Update")), key=lambda kv: -kv[1])
        log.append({"k": k, "x": x, "removed": cr.get("removed"), "clear_timing_ms": cr.get("timing_ms"),
                    "seen_s": round(time.time() - t0, 3), "uploads": v["gpu_uploads"] - before["gpu_uploads"],
                    "patches": v.get("gpu_patches", 0) - before.get("gpu_patches", 0), "gpu_patch_ms": v.get("gpu_patch_ms"),
                    "occ_cells_resampled": v.get("occ_cells_resampled"), "occ_cells_changed": v.get("occ_cells_changed"),
                    "occ_full_refreshes": v.get("occ_full_refreshes"), "occ_local_refreshes": v.get("occ_local_refreshes"),
                    "worst_frame_ms": wv.get("Frame"), "worst_frame_stage": stages[0] if stages else None,
                    "occ_refresh_ms": v.get("occ_refresh_ms"), "gpu_upload_ms": v.get("gpu_upload_ms"), "cpu_frame_ms_after": et.get("cpuFrameTime"),
                    "fps_after": et.get("fps"), "ground_bricks": v.get("gpu_ground_bricks"), "bricks": v.get("gpu_bricks"),
                    "ledger_m3": v["mass"] - (v["source_in_m3"] - v["outfall_out_m3"])})
        print(json.dumps(log[-1]))
        time.sleep(max(0.0, args.every - (time.time() - t0)))
    time.sleep(args.settle)
    v1 = vol(api)
    if rec: rec.wait()
    # what the springs put in and the outfalls took out since the start, against what the volume now holds
    inflow = (v1["source_in_m3"] - v0["source_in_m3"]) - (v1["outfall_out_m3"] - v0["outfall_out_m3"])
    cols = api.debug("water_av_column", {"points": [[args.x0 + args.step * k + 0.5, args.z + 0.5] for k in range(min(args.digs, 64))]}).get("columns", [])
    med = lambda a: sorted(a)[len(a) // 2]
    summary = {"digs": args.digs, "pits_m3": pit_m3, "stored_before_m3": v0["mass"], "stored_after_m3": v1["mass"],
               "stored_rise_m3": v1["mass"] - v0["mass"], "net_inflow_m3": inflow, "ledger_m3": v1["mass"] - (v1["source_in_m3"] - v1["outfall_out_m3"]),
               "pit_columns_wet": sum(1 for c in cols if c.get("surface_y") is not None),
               "occ_refresh_ms_median": med([e["occ_refresh_ms"] for e in log]), "occ_refresh_ms_max": max(e["occ_refresh_ms"] for e in log),
               "gpu_upload_ms_median": med([e["gpu_upload_ms"] for e in log]), "gpu_upload_ms_max": max(e["gpu_upload_ms"] for e in log),
               "every_dig_seen": all(e["uploads"] + e["patches"] >= 1 for e in log), "ground_bricks_end": v1.get("gpu_ground_bricks"),
               "worst_frame_ms_median": med([e["worst_frame_ms"] or 0.0 for e in log]), "worst_frame_ms_max": max(e["worst_frame_ms"] or 0.0 for e in log),
               "occ_full_refreshes_during": (v1.get("occ_full_refreshes", 0) - v0.get("occ_full_refreshes", 0)),
               "removed_total": sum(e["removed"] or 0 for e in log)}
    (B.EV / f"{name}.json").write_text(json.dumps({"summary": summary, "digs": log}, indent=1))
    print(json.dumps(summary))


if __name__ == "__main__":
    main()
