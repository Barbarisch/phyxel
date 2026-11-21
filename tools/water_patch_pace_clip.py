"""WaterCore 58.3 - the clip for the owner's call on the staged patch wake: the SAME push into the SAME recorded lake, the patch made
at once (shipped) vs built over frames (water_coupling patch_pace, off by default). The owner (2026-11-21): "i have no idea if that
delay is ok. i cant predict the future. we need to test everything" - so both are shown, side by side.

WaterBench_Creek (port 8114, Release): 57.6's rig - the sand bed, the 60 x 60 m lake 2 m deep written into the record, the camera 8 m
up (water_play_scene.CHURN_CAM), the push water_patch_sleep_split.py makes (the lake's centre, radius 2 m, strength 8, down). A warm-up
push first (its patch returns to the record), so both takes start with the patch spare and the pipelines warm, as live play after the
first splash. Each take: 2 s of the still lake, the push, 8 s more; then the patch is let go back to the record before the next take.
The frame time is read once just before the push and once 1.5 s after it (the worst frame between); nothing is polled while it
records (D9). Recorded from the window (gdigrab: /api/screenshot omits pond water). Nothing rises; patch_pace goes back to off.

    python tools/water_patch_pace_clip.py [--url http://127.0.0.1:8114] [--tag s58_patch_pace] [--crop W:H:X:Y]
Writes docs/evidence/water_core_m4/<tag>_{now,staged}.mp4, <tag>_side_by_side.mp4, <tag>.json.
"""
import argparse, json, pathlib, shutil, subprocess, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set
import water_play_scene as P
from water_sweep_clip import write_srt, record, caption, EV

PUSH = {"x": P.X0 + P.W / 2, "y": P.TOP + 1.9, "z": P.Z0 + P.W / 2, "radius": 2.0, "strength": 8.0, "dx": 0.0, "dy": -1.0, "dz": 0.0}


def wait_back(api, limit=60.0):
    """the patch back in the record (no live volume, no retiring one) - polled between takes only"""
    t0, seen = time.time(), False
    while time.time() - t0 < limit:
        vols = api.debug("water_av_list").get("volumes", [])
        seen = seen or bool(vols)
        if seen and not vols: time.sleep(3.0); return round(time.time() - t0, 1)
        time.sleep(1.0)
    return None


def take(api, ff, a, mode):
    api.debug("water_coupling", {"patch_pace": mode == "staged", "patch_pace_reset": True})
    camera_set(api, P.CHURN_CAM)
    time.sleep(3.0)
    raw = EV / f"{a.tag}_{mode}_raw.mp4"
    rec = record(ff, a.window, a.seconds, raw)
    time.sleep(2.0)
    api.get("/api/debug/engine_timing")   # resets the worst-frame window
    t_push = 2.0
    st = api.debug("water_av_impulse", PUSH)
    time.sleep(1.5)
    worst = api.get("/api/debug/engine_timing").get("frame_ms_max_since_last_read")
    rec.wait()
    pp = api.debug("water_coupling", {}).get("slept", {}).get("patch_paced", {})
    back = wait_back(api)
    if mode == "now":
        label = f"AT ONCE (shipped): the patch is made in the push's frame - worst frame {worst:.0f} ms"
    else:
        label = (f"BUILT OVER FRAMES (58.3, off by default): the push waits {pp.get('last_frames', 0)} frames - worst frame {worst:.0f} ms")
    srt = EV / f"{a.tag}_{mode}.srt"
    write_srt(srt, [(0.0, "the recorded 60 x 60 m lake, 2 m deep - a push at its centre at 2 s"), (t_push, label)], a.seconds)
    caption(ff, raw, srt, EV / f"{a.tag}_{mode}.mp4", a.crop)
    raw.unlink(missing_ok=True)
    return {"mode": mode, "push": st, "worst_frame_ms_push_window": worst, "patch_paced": pp, "back_in_record_s": back}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8114")
    ap.add_argument("--window", default="WaterBench_Creek")
    ap.add_argument("--tag", default="s58_patch_pace")
    ap.add_argument("--seconds", type=float, default=10.0)
    ap.add_argument("--crop", default="")
    a = ap.parse_args()
    ff = shutil.which("ffmpeg")
    if not ff: raise SystemExit("ffmpeg not on PATH")
    if P.foreign(): raise SystemExit(f"other phyxel processes share the GPU: {P.foreign()}")
    api = Api(a.url); api.wait_status(300)
    try: api.debug("editor_panels", {"item_equipper": False, "tool_panels": False})
    except Exception as e: print("editor_panels:", e)
    if "error" in P.ps(api):
        api.post("/api/entity/spawn", {"type": "animated", "position": {"x": 2.0, "y": P.TOP + 1.2, "z": 0.0}, "id": "player"})
    api.debug("water_av_realtime", {"on": True})
    out = {"bench": "WaterBench_Creek", "push": PUSH, "camera": P.CHURN_CAM, "takes": []}
    try:
        P.destroy_all(api); P.clear_record(api); P.lay_bed(api)
        time.sleep(30.0)   # the far skin's re-read of the clear (57.4 item 2)
        api.post("/api/entity/move", {"id": "player", "position": {"x": 2.0, "y": P.TOP + 1.2, "z": 0.0}})
        out["record_lake"] = api.debug("water_av_record_lake", {"x1": P.X0, "z1": P.Z0, "x2": P.X0 + P.W - 1, "z2": P.Z0 + P.W - 1, "bottom": P.TOP, "top": P.TOP + 2.0})
        camera_set(api, P.CHURN_CAM)
        time.sleep(4.0)
        api.debug("water_coupling", {"patch_pace": False})
        api.debug("water_av_impulse", PUSH)   # the warm-up: the spare and the pipelines made
        out["warmup_back_s"] = wait_back(api)
        for mode in ("now", "staged"):
            out["takes"].append(take(api, ff, a, mode))
    finally:
        api.debug("water_coupling", {"patch_pace": False})
        out["patch_pace_restored"] = api.debug("water_coupling", {}).get("slept", {}).get("patch_pace")
        P.destroy_all(api)
        (EV / f"{a.tag}.json").write_text(json.dumps(out, indent=1))
    subprocess.run([ff, "-y", "-loglevel", "error", "-i", f"{a.tag}_now.mp4", "-i", f"{a.tag}_staged.mp4", "-filter_complex",
                    "[0:v][1:v]hstack=inputs=2[v]", "-map", "[v]", "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "21",
                    f"{a.tag}_side_by_side.mp4"], check=True, cwd=str(EV))
    print(json.dumps([{k: t[k] for k in ("mode", "worst_frame_ms_push_window", "back_in_record_s")} | {"paced": t["patch_paced"]} for t in out["takes"]], indent=1))
    print("wrote", EV / f"{a.tag}_side_by_side.mp4")


if __name__ == "__main__":
    main()
