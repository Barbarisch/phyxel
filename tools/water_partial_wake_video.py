"""WaterCore.md 26.31 - the owner's look at (c), the partial wake: the same stone dropped into the same pond twice, the partial
wake OFF and ON, recorded from the window (gdigrab - /api/screenshot omits pond water) and put side by side with the solver's
time and the awake columns written on each half (polled live, ~4 a second).

    python tools/water_partial_wake_video.py [--size 46x28] [--at corner|centre] [--seconds 16] [--tag pw_look] [--push impulse]
    the owner's close view (2026-10-18, "too far away"): --push impulse --pose 11,18.3,27.3,-150,-14 (low, inside the kerb, ~5 m off)
Writes docs/evidence/water_core_h1/<tag>_off.mp4, <tag>_on.mp4 and <tag>_side_by_side.mp4 (+ .json with the polled rows).
"""
import argparse, json, pathlib, shutil, subprocess, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set, world_job

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_h1"


def srt_time(t):
    ms = int(round(t * 1000)); h, ms = divmod(ms, 3600000); m, ms = divmod(ms, 60000); s, ms = divmod(ms, 1000)
    return f"{h:02}:{m:02}:{s:02},{ms:03}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8114")
    ap.add_argument("--window", default="WaterBench_Creek")
    ap.add_argument("--size", default="46x28")
    ap.add_argument("--at", choices=["corner", "near", "centre"], default="near", help="corner = the far (north-west) corner; near = the south-west corner, nearest the camera")
    ap.add_argument("--seconds", type=float, default=16.0)
    ap.add_argument("--stone", type=float, default=1.0, help="the stone's edge (m; spawn_gpu_particle scale)")
    ap.add_argument("--push", choices=["stone", "impulse"], default="stone", help="impulse = the same push in both takes (water_av_impulse; the GPU stone tumbles differently every drop - the owner read that as a difference)")
    ap.add_argument("--strength", type=float, default=4.0, help="the impulse's strength (--push impulse)")
    ap.add_argument("--quiet-scale", type=float, default=1.0, help="26.34: the ON take's per-column freeze thresholds x this (water_av_partial_wake {quiet_scale})")
    ap.add_argument("--pose", default=None, help="x,y,z,yaw,pitch")
    ap.add_argument("--still", action="store_true", help="only set up the pond and the camera, grab one frame (framing check)")
    ap.add_argument("--tag", default="pw_look")
    ap.add_argument("--crop", default="954:560:243:60", help="w:h:x:y of the window capture that is the 3-D viewport (the editor docks panels round it)")
    args = ap.parse_args()
    ff = shutil.which("ffmpeg")
    api = Api(args.url); api.wait_status(300)
    l, w = (int(v) for v in args.size.split("x"))
    x0, z0 = 2, 0
    pose = {"x": x0 + l / 2, "y": 16 + 0.62 * max(l, w), "z": z0 + w + 0.42 * max(l, w), "yaw": -90.0, "pitch": -48.0}
    if args.pose:
        p = [float(v) for v in args.pose.split(",")]; pose = {"x": p[0], "y": p[1], "z": p[2], "yaw": p[3], "pitch": p[4]}
    sx, sz = {"corner": (x0 + 4.0, z0 + 4.0), "near": (x0 + 4.0, z0 + w - 4.0), "centre": (x0 + l / 2, z0 + w / 2)}[args.at]
    try: api.debug("editor_panels", {"item_equipper": False, "tool_panels": False})   # the viewport clear (water_bench refshots)
    except Exception as e: print("editor_panels:", e)
    # the look of the rig (not the water): a sand bed instead of grass under the pond (blades showed through the water) and a
    # stone kerb two blocks high round it (the volume's box walls are invisible) - world edits, re-laid every run (an engine restart drops them)
    world_job(api, "/api/world/clear", {"x1": x0, "y1": 15, "z1": z0, "x2": x0 + l - 1, "y2": 15, "z2": z0 + w - 1})
    world_job(api, "/api/world/fill", {"x1": x0, "y1": 15, "z1": z0, "x2": x0 + l - 1, "y2": 15, "z2": z0 + w - 1}, material="Sand")
    for b in ((x0 - 1, z0 - 1, x0 - 1, z0 + w), (x0 + l, z0 - 1, x0 + l, z0 + w), (x0 - 1, z0 - 1, x0 + l, z0 - 1), (x0 - 1, z0 + w, x0 + l, z0 + w)):
        world_job(api, "/api/world/fill", {"x1": b[0], "y1": 16, "z1": b[1], "x2": b[2], "y2": 17, "z2": b[3]}, material="StoneBricks")   # two high: the water sits inside the rim (level with its top, a trough of a few mm showed the kerb's face as a bright line - "the water pulls away from the edges", the owner)
    api.debug("water_av_realtime", {"on": True})
    api.debug("water_coupling", {"enabled": True, "solids": True})
    out = {"pose": pose, "size_m": [l, w], "stone_at": [sx, sz], "stone_edge_m": args.stone, "runs": {}}

    def pond(partial):
        for v in api.debug("water_av_list").get("volumes", []): api.debug("water_av_destroy", {"id": v["id"]})
        box = {"x1": x0, "y1": 16, "z1": z0, "x2": x0 + l - 1, "y2": 17, "z2": z0 + w - 1}
        vid = api.debug("water_av_create", {**box, "cellSize": 1 / 9, "backend": "gpu", "auto_sleep": False, "partial_wake": partial})["volume"]["id"]
        api.debug("place_water_box", {"target": "core", **{**box, "y2": 16}, "mass": 1.0})
        camera_set(api, pose)
        time.sleep(6.0)   # placed water settles (and, with the partial wake, freezes)
        return vid

    if args.still:
        pond(True)
        subprocess.run([ff, "-y", "-loglevel", "error", "-f", "gdigrab", "-i", f"title={args.window}", "-frames:v", "1", "-vf", f"crop={args.crop}", str(EV / f"{args.tag}_still.png")], check=True)
        print("wrote", EV / f"{args.tag}_still.png"); return

    for name, partial in (("off", False), ("on", True)):
        api.debug("water_av_partial_wake", {"quiet_scale": args.quiet_scale if partial else 1.0})
        vid = pond(partial)
        mp4 = EV / f"{args.tag}_{name}.mp4"
        rec = subprocess.Popen([ff, "-y", "-loglevel", "error", "-f", "gdigrab", "-framerate", "30", "-t", str(args.seconds),
                                "-i", f"title={args.window}", "-c:v", "libx264", "-pix_fmt", "yuv420p", str(mp4)])
        t0 = time.time()
        time.sleep(1.5)
        if args.push == "impulse":
            api.debug("water_av_impulse", {"x": sx, "y": 16.9, "z": sz, "radius": 0.5, "strength": args.strength, "dy": -1.0})
        else:
            api.debug("spawn_gpu_particle", {"x": sx, "y": 20.0, "z": sz, "material": "Stone", "scale": args.stone, "lifetime": args.seconds - 1.0})   # gone before the next take
        t_stone = time.time() - t0
        rows = []
        while rec.poll() is None:
            v = next((x for x in api.debug("water_av_list")["volumes"] if x["id"] == vid), {})
            rows.append({"t": round(time.time() - t0, 2), "solver_ms": v.get("gpu_device_ms"), "awake": v.get("awake_columns"),
                         "frozen": v.get("frozen_columns"), "asleep": v.get("asleep")})
            time.sleep(0.25)
        out["runs"][name] = {"stone_s": round(t_stone, 2), "rows": rows}
        # the caption track: what the solver costs and how much of the pond steps, as it happens
        lines = []
        for i, r in enumerate(rows):
            t1 = rows[i + 1]["t"] if i + 1 < len(rows) else r["t"] + 0.25
            total = (r["awake"] or 0) + (r["frozen"] or 0)
            stepped = f"stepping {r['awake']} of {total} columns" if partial and total else "stepping the whole pond"
            ms = 0.0 if r["asleep"] else (r["solver_ms"] or 0.0)
            label = (f"PARTIAL WAKE ON (freeze x{args.quiet_scale:g})" if args.quiet_scale != 1.0 else "PARTIAL WAKE ON") if partial else "PARTIAL WAKE OFF"
            lines.append(f"{i + 1}\n{srt_time(r['t'])} --> {srt_time(t1)}\n{label}  |  solver {ms:.2f} ms  |  {stepped}\n")
        (EV / f"{args.tag}_{name}.srt").write_text("\n".join(lines), encoding="utf-8")
        print(name, "recorded;", "solver ms median", sorted(r["solver_ms"] or 0 for r in rows)[len(rows) // 2])
    api.debug("water_av_partial_wake", {"quiet_scale": 1.0})
    for v in api.debug("water_av_list").get("volumes", []): api.debug("water_av_destroy", {"id": v["id"]})
    (EV / f"{args.tag}.json").write_text(json.dumps(out, indent=1))
    # side by side: each half captioned, scaled to 960 wide; run in EV so the subtitle paths need no escaping
    style = "FontName=Arial,FontSize=14,PrimaryColour=&H00FFFFFF,OutlineColour=&H00000000,BorderStyle=3,Outline=1,Alignment=2,MarginV=6"
    fc = (f"[0:v]crop={args.crop},scale=960:-2,subtitles={args.tag}_off.srt:force_style='{style}'[a];"
          f"[1:v]crop={args.crop},scale=960:-2,subtitles={args.tag}_on.srt:force_style='{style}'[b];[a][b]hstack=inputs=2[v]")
    subprocess.run([ff, "-y", "-loglevel", "error", "-i", f"{args.tag}_off.mp4", "-i", f"{args.tag}_on.mp4", "-filter_complex", fc,
                    "-map", "[v]", "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "20", f"{args.tag}_side_by_side.mp4"], check=True, cwd=str(EV))
    print("wrote", EV / f"{args.tag}_side_by_side.mp4")


if __name__ == "__main__":
    main()
