"""MaterialSimPlan 6i - a video of the hybrid (E2) dam break + U-tube in the lab rig, played at SIM speed.

The rig is the hand-placed MatSimLab project (port 8120; Stone walls by /api/world/fill, NOT generator output) and the
lab's raw-cell view (cells as cubes, particles as cubes of the same volume). The sim is stepped by the API two ticks
(1/30 s) per frame and the engine's own screenshot is taken after each step, so every video frame is exactly 1/30 s of
sim time whatever the tick costs (1/9 m runs ~14x slower than real time). Captions read the lab's stats.

    python tools/matsim_hybrid_video.py --cpm 3 [--seconds 36] [--still]
Writes docs/evidence/matsim_video/hybrid_<cpm>.mp4 (+ .srt, .json).
"""
import argparse, json, pathlib, shutil, subprocess, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import matsim_lab as L
from water_bench import Api, camera_set, world_job

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "matsim_video"
FPS, TPF = 30, 2   # video frames per second, sim ticks (1/60 s) per frame


def srt_time(t):
    ms = int(round(t * 1000)); h, ms = divmod(ms, 3600000); m, ms = divmod(ms, 60000); s, ms = divmod(ms, 1000)
    return f"{h:02}:{m:02}:{s:02},{ms:03}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8120")
    ap.add_argument("--cpm", type=int, default=3)
    ap.add_argument("--seconds", type=float, default=36.0, help="sim seconds after the dam is removed")
    ap.add_argument("--before", type=float, default=1.0, help="sim seconds shown before the dam is removed")
    ap.add_argument("--pose", default="0,38,16,0,-60", help="x,y,z,yaw,pitch")
    ap.add_argument("--still", action="store_true", help="framing check: one frame, no run")
    a = ap.parse_args()
    ff = shutil.which("ffmpeg")
    api = Api(a.url); api.wait_status(120)
    EV.mkdir(parents=True, exist_ok=True)
    frames = EV / f"frames_{a.cpm}"
    if frames.exists(): shutil.rmtree(frames)
    frames.mkdir()
    try: api.debug("editor_panels", {"item_equipper": False, "tool_panels": False})
    except Exception as e: print("editor_panels:", e)
    L.fill(api, L.DAM); L.wait_occupancy()
    L.must(L.ms(api, "create", {"min": L.BOX_MIN, "size": L.BOX_SIZE, "cells_per_m": a.cpm, "particles": "hybrid"}), "create")
    L.must(L.ms(api, "fill_box", {"material": "Water", "min": {"x": 8, "y": 16, "z": 8}, "size": {"x": 3, "y": 3, "z": 8}, "fraction": 1.0}), "fill")
    L.must(L.ms(api, "fill_box", {"material": "Water", "min": {"x": 8, "y": 16, "z": 17}, "size": {"x": 8, "y": 5, "z": 7}, "fraction": 1.0}), "fill")
    p = [float(v) for v in a.pose.split(",")]
    pose = {"x": p[0], "y": p[1], "z": p[2], "yaw": p[3], "pitch": p[4]}
    got = camera_set(api, pose)
    L.must(L.ms(api, "view", {"on": True}), "view")
    rows, n = [], 0

    def frame(phase, t_sim):
        nonlocal n
        time.sleep(0.05)   # let the redrawn view reach a presented frame
        r = api.get("/api/screenshot", timeout=60)
        src = pathlib.Path(r["path"])
        if not src.is_absolute(): src = L.ROOT / src
        dst = frames / f"f{n:05d}.png"
        shutil.move(str(src), dst)
        st = L.ms(api, "stats") if n % 5 == 0 or not rows else None
        if st is not None:
            rows.append({"frame": n, "phase": phase, "t_sim": round(t_sim, 3), "tick": st["tick"], "particles": st.get("particles", 0),
                         "ledger_exact": st["ledger"]["exact"], "last_tick_ms": st.get("last_tick_ms")})
        n += 1

    if a.still:
        L.must(L.ms(api, "step", {"n": 1}), "step"); frame("still", 0.0)
        print("wrote", frames / "f00000.png", "pose", got); return
    t0 = time.time()
    nb = int(round(a.before * FPS))
    for i in range(nb):
        L.must(L.ms(api, "step", {"n": TPF}), "step"); frame("before", (i + 1 - nb) / FPS)
    world_job(api, "/api/world/clear", L.DAM); L.wait_occupancy()
    L.must(L.ms(api, "resolidify"), "resolidify")
    na = int(round(a.seconds * FPS))
    for i in range(na):
        L.must(L.ms(api, "step", {"n": TPF}), "step"); frame("after", (i + 1) / FPS)
        if i % 150 == 0: print(f"{i}/{na} frames, {rows[-1]}, {time.time() - t0:.0f} s wall", flush=True)
    L.ms(api, "view", {"on": False})
    L.fill(api, L.DAM)
    label = {3: "1/3 m cells", 9: "1/9 m cells"}.get(a.cpm, f"1/{a.cpm} m")
    lines = []
    for k, r in enumerate(rows):
        t1 = rows[k + 1]["frame"] / FPS if k + 1 < len(rows) else (n) / FPS
        when = "dam in place" if r["phase"] == "before" else f"{r['t_sim']:5.2f} s after the dam is removed"
        lines.append(f"{k + 1}\n{srt_time(r['frame'] / FPS)} --> {srt_time(t1)}\nMatSim hybrid, {label}  |  {when}  (sim time, real speed)\n"
                     f"{r['particles']:,} particles in flight  |  water ledger {'exact' if r['ledger_exact'] else 'NOT EXACT'}\n")
    srt = EV / f"hybrid_{a.cpm}.srt"
    srt.write_text("\n".join(lines), encoding="utf-8")
    (EV / f"hybrid_{a.cpm}.json").write_text(json.dumps({"cpm": a.cpm, "pose_asked": pose, "pose_got": got, "fps": FPS, "ticks_per_frame": TPF,
                                                         "frames": n, "wall_s": round(time.time() - t0, 1), "rows": rows}, indent=1))
    style = "FontName=Arial,FontSize=14,PrimaryColour=&H00FFFFFF,OutlineColour=&H00000000,BorderStyle=3,Outline=1,Alignment=2,MarginV=8"
    subprocess.run([ff, "-y", "-loglevel", "error", "-framerate", str(FPS), "-i", f"frames_{a.cpm}/f%05d.png",
                    "-vf", f"crop=956:470:241:60,scale=1280:-2,subtitles=hybrid_{a.cpm}.srt:force_style='{style}'",
                    "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "22", f"hybrid_{a.cpm}.mp4"], check=True, cwd=str(EV))
    shutil.rmtree(frames)
    print("wrote", EV / f"hybrid_{a.cpm}.mp4")


if __name__ == "__main__":
    main()
