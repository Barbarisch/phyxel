"""WaterCore.md §54.2 (W5.2) - the owner's clip: a reach of a GENERATED river simulated where the player is - fed and drained at its
edges from the record - wood carried through it, then a stone dam built across it.

WaterBench_RiverCarve (port 8113), the engine's defaults; W5.1's pose over the 51.14 reach (-1729, -1040). Timeline (captioned):
  0-3 s   the record (still spans; W5.1's current carries floating things, nothing is simulated);
  3 s     water_av_river_reach at the site (40 x 40 m, half_m 20) - the reach is simulated from here (its make stalls a frame: shown);
  6 s     five 1/3 m wood blocks dropped on the river (W5.1's pick, --skip columns);
  --dam s a stone dam across the river 6 m downstream of the site, perpendicular to the record's current there: 2 x 2 m stone
          columns every metre along the line on the river's WATER columns only, each from its bed up to y 20 (crest y 21, ~0.9 m
          over the stage) - removed again at the end (the water columns from their beds: nothing of the banks touched);
  to the end: the water above it rises (measured +0.39 m in 25 s, tools/water_w52_dam.py); below it the current does NOT die on this
  river - the open downstream edge holds the record's level there (20.22 m), above the dammed pool's, and refills it backwards (-0.20
  m/s along the river): the record's stage rises downstream here (51.14 - the owner's call, WaterCore 54.2).
Recorded with gdigrab, captioned with the phase, the reach's GPU time and the frame rate.

    python tools/water_w52_clip.py [--seconds 50] [--dam 22] [--tag w52_clip]
Writes docs/evidence/water_core_m4/<tag>.mp4 / .srt / .json.
"""
import argparse, json, math, pathlib, shutil, subprocess, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set, world_job
from water_w5_river_current import pick, tracks_xz, SITES
from water_w5_clip import POSE, srt_time

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_m4"


def dam_columns(api):
    c = SITES["river"]["c"]; lvl = SITES["river"]["level"]
    q = api.debug("water_query", {"points": [[c[0] + 0.5, lvl - 0.15, c[1] + 0.5]]})["points"][0]
    d = (q.get("river") or {}).get("dir") or [1.0, 0.0]
    n = math.hypot(*d); d = [d[0] / n, d[1] / n]
    centre = (c[0] + 6.0 * d[0], c[1] + 6.0 * d[1]); perp = (-d[1], d[0])
    line = [(centre[0] + t * perp[0], centre[1] + t * perp[1]) for t in range(-14, 15)]
    rows = api.debug("water_query", {"points": [[x, lvl - 0.15, z] for x, z in line]})["points"]
    out = []
    for (x, z), r in zip(line, rows):   # the river's water columns only, each from its bed: the dam is removed exactly afterwards
        if not r.get("in_water"): continue
        depth = (r.get("river") or {}).get("depth_m") or 0.0
        out.append((int(math.floor(x)), int(math.floor(z)), int(math.floor(r["surface_y"] - depth + 1e-3))))
    return d, out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8113")
    ap.add_argument("--window", default="WaterBench_RiverCarve")
    ap.add_argument("--seconds", type=float, default=50.0)
    ap.add_argument("--dam", type=float, default=22.0)
    ap.add_argument("--crop", default="954:560:243:60")
    ap.add_argument("--skip", type=int, default=12)
    ap.add_argument("--tag", default="w52_clip")
    a = ap.parse_args()
    ff = shutil.which("ffmpeg")
    api = Api(a.url); api.wait_status(600)
    try: api.debug("editor_panels", {"item_equipper": False, "tool_panels": False})
    except Exception as e: print("editor_panels:", e)
    # the water core steps only with realtime ON - no game session turns it on (only this route; every live water tool since 51 does
    # it, WaterCore 54.2 found it: the first w52 run measured an unstepped reach - its "current" was the seed's)
    print("realtime:", api.debug("water_av_realtime", {"on": True}))
    for v in api.debug("water_av_list").get("volumes", []):
        if v.get("is_reach"): api.debug("water_av_river_reach", {"release": v["id"]})
    camera_set(api, POSE); time.sleep(12.0)
    wet, _ = pick(api, "river")
    drops = wet[a.skip:a.skip + 5]
    if len(drops) < 5: raise SystemExit(f"only {len(drops)} wet columns after skip {a.skip}")
    speed = sum(d["river"]["speed_mps"] for d in drops) / len(drops)
    ddir, dam = dam_columns(api)
    if not dam: raise SystemExit("no wet columns on the dam's line")
    log = (EV / f"{a.tag}_debris_tmp.csv").resolve()
    api.debug("particle_log", {"action": "start", "file": str(log)})
    raw = EV / f"{a.tag}_raw.mp4"
    rec = subprocess.Popen([ff, "-y", "-loglevel", "error", "-f", "gdigrab", "-framerate", "30", "-t", str(a.seconds), "-i", f"title={a.window}",
                            "-c:v", "libx264", "-pix_fmt", "yuv420p", str(raw)])
    t0 = time.time(); reach = None; dropped = False; dammed = None; rows = []
    while rec.poll() is None:
        t = time.time() - t0
        if reach is None and t >= 3.0:
            c = SITES["river"]["c"]
            reach = api.debug("water_av_river_reach", {"x": c[0], "z": c[1], "half_m": 20.0, "y": SITES["river"]["level"]}, timeout=300)
            print("reach:", {k: reach.get(k) for k in ("success", "id", "error", "min", "max", "ring_columns", "make_ms")}, flush=True)
        if reach is not None and not dropped and t >= 6.0:
            for d in drops:
                api.debug("spawn_gpu_particle", {"x": d["p"][0], "y": d["surface_y"] + 1.2, "z": d["p"][2], "material": "Wood",
                                                 "scale": 1.0 / 3.0, "lifetime": 120.0, "spin": 0.0})
            dropped = True
        if dammed is None and t >= a.dam:
            dammed = round(t, 2)
            for x, z, yb in dam:
                world_job(api, "/api/world/fill", {"x1": x, "y1": yb, "z1": z, "x2": x + 1, "y2": 20, "z2": z + 1}, material="Stone")
            print(f"dam placed at {dammed} s ({len(dam)} columns)", flush=True)
        gms = None
        if reach and reach.get("id"):
            try:
                v = next((v for v in api.debug("water_av_list").get("volumes", []) if v.get("id") == reach["id"]), None)
                gms = v.get("gpu_device_ms") if v else None
            except Exception: pass
        try: fps = api.get("/api/debug/engine_timing").get("fps")
        except Exception: fps = None
        rows.append({"t": round(t, 2), "fps": fps, "reach_gpu_ms": gms})
        time.sleep(0.25)
    api.debug("particle_log", {"action": "stop"})
    tr = tracks_xz(log); log.unlink(missing_ok=True)
    debris = []
    for slot, ps in tr.items():
        if len(ps) < 10: continue
        k0 = next((i for i, p in enumerate(ps) if p[2] < drops[0]["surface_y"] + 0.6), None)
        if k0 is None: continue
        p0, p1 = ps[k0], ps[-1]
        debris.append({"slot": slot, "t_in_water_s": round(p1[0] - p0[0], 2), "drift_m": round(math.hypot(p1[1] - p0[1], p1[3] - p0[3]), 2),
                       "start": [round(p0[1], 2), round(p0[3], 2)], "end": [round(p1[1], 2), round(p1[3], 2)], "y_last": round(p1[2], 3)})
    lines = []
    for i, r in enumerate(rows):
        t1 = rows[i + 1]["t"] if i + 1 < len(rows) else r["t"] + 0.25
        if r["t"] < 3.0: ph = "a generated river - the record (spans; nothing simulated)"
        elif r["t"] < 6.0: ph = "a 40 x 40 m reach SIMULATED here, fed and drained at its edges from the record"
        elif dammed is None or r["t"] < dammed: ph = f"5 wood blocks dropped - carried by the simulated river (the record: {speed:.2f} m/s here)"
        else: ph = "a stone dam across it: the water above it rises (+0.39 m measured, w52_dam.json)"
        g = r.get("reach_gpu_ms")
        lines.append(f"{i + 1}\n{srt_time(r['t'])} --> {srt_time(t1)}\n{ph}\nt {r['t']:.1f} s | {(r['fps'] or 0):.0f} fps"
                     + (f" | reach GPU {g:.1f} ms" if isinstance(g, (int, float)) else "") + "\n")
    (EV / f"{a.tag}.srt").write_text("\n".join(lines), encoding="utf-8")
    out = {"bench": "WaterBench_RiverCarve", "pose": POSE, "reach": reach, "dam_at_s": dammed, "dam_columns": dam, "record_dir": ddir,
           "route_speed_mps": speed, "drops": [{"p": d["p"], "surface_y": d["surface_y"], "river": d["river"]} for d in drops],
           "debris": debris, "rows": rows}
    (EV / f"{a.tag}.json").write_text(json.dumps(out, indent=1))
    style = "FontName=Arial,FontSize=12,PrimaryColour=&H00FFFFFF,OutlineColour=&H00000000,BorderStyle=3,Outline=1,Alignment=2,MarginV=6"
    subprocess.run([ff, "-y", "-loglevel", "error", "-i", raw.name, "-vf", f"crop={a.crop},scale=1280:-2,subtitles={a.tag}.srt:force_style='{style}'",
                    "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "21", f"{a.tag}.mp4"], check=True, cwd=str(EV))
    raw.unlink(missing_ok=True)
    for x, z, yb in dam:   # the dam out again (only what was put: the water columns from their beds up)
        world_job(api, "/api/world/clear", {"x1": x, "y1": yb, "z1": z, "x2": x + 1, "y2": 20, "z2": z + 1})
    if reach and reach.get("id"): print("release:", api.debug("water_av_river_reach", {"release": reach["id"]}))
    print("debris:", debris)
    print("wrote", EV / f"{a.tag}.mp4")


if __name__ == "__main__":
    main()
