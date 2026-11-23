"""WaterCore 56 - the sign-off sweep (§0a item 5): the two clips the built-not-signed-off small-body items never had.

  --item wade    A4 (§48) wading, on WaterBench_Small (port 8111): the channel rig (x 66-77, z 14-15, floor top y 16, 0.8 m of
                 water in a core volume, as tools/water_wade_s7.py fills it). One NPC patrols the channel's centre line (the same
                 character coupling as the player: water_coupling characters) while a fixed free camera looks at it from the side.
                 Two takes - characters OFF (the water is not fed: before A4) and ON (shipped) - then side by side.
                 Writes <tag>_off.mp4, <tag>_on.mp4, <tag>_side_by_side.mp4, <tag>.json.
  --item scenes  A2i (§47) the water across a scene change, on WaterBench_Scenes (port 8112): scene 'pond' (the Small bench's
                 4 x 4 m pond, slept into the record), scene 'dry' (a flat floor, same chunk, no water). Wood dropped on the pond
                 wakes it; with the pond AWAKE the scene goes to 'dry' (no water follows), then back to 'pond' (the pond is there,
                 once, asleep); wood dropped again wakes it again. One take, captioned from the logged events.
                 Writes <tag>.mp4, <tag>_raw.mp4, <tag>.srt, <tag>.json.

Recorded from the window (gdigrab: /api/screenshot omits pond water). D9 (§53): polling water_av_list in a clip adds ~80 ms frames
the game does not have - the clips read the water state only at event boundaries, never while frames matter. Only wood is dropped
(it floats by its density), nothing is made to rise. Every control goes back to the shipped default at the end, even on a failure.
    python tools/water_sweep_clip.py --item wade|scenes [--tag ...] [--crop W:H:X:Y]
"""
import argparse, json, pathlib, shutil, subprocess, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_m4"
STYLE = "FontName=Arial,FontSize=12,PrimaryColour=&H00FFFFFF,OutlineColour=&H00000000,BorderStyle=3,Outline=1,Alignment=2,MarginV=6"


def srt_time(t):
    ms = int(round(max(t, 0.0) * 1000)); h, ms = divmod(ms, 3600000); m, ms = divmod(ms, 60000); s, ms = divmod(ms, 1000)
    return f"{h:02}:{m:02}:{s:02},{ms:03}"


def write_srt(path, cues, end):
    """cues: [(t, text)] in order; each runs to the next (the last to end)."""
    out = []
    for i, (t, text) in enumerate(cues):
        t1 = cues[i + 1][0] if i + 1 < len(cues) else end
        out.append(f"{i + 1}\n{srt_time(t)} --> {srt_time(t1)}\n{text}\n")
    path.write_text("\n".join(out), encoding="utf-8")


def record(ff, window, seconds, raw):
    return subprocess.Popen([ff, "-y", "-loglevel", "error", "-f", "gdigrab", "-framerate", "30", "-t", str(seconds), "-i",
                             f"title={window}", "-c:v", "libx264", "-pix_fmt", "yuv420p", str(raw)])


def caption(ff, raw, srt, out, crop):
    vf = (f"crop={crop}," if crop else "") + f"scale=1280:-2,subtitles={srt.name}:force_style='{STYLE}'"
    subprocess.run([ff, "-y", "-loglevel", "error", "-i", raw.name, "-vf", vf, "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "21",
                    out.name], check=True, cwd=str(EV))


def fps(api):
    try: return api.get("/api/debug/engine_timing").get("fps")
    except Exception: return None


# ---------------------------------------------------------------- A4 wading
CH_BOX = {"x1": 66, "y1": 16, "z1": 14, "x2": 77, "y2": 18, "z2": 15}
DEPTH = 0.8
LANE_Z = 15.0
# down the channel from its east end, low (the bank top is y 17, the water 16.8): 1-5 cm waves read only at a grazing angle
# (from the side at 6 m / 42 deg both takes looked the same - sweep take 1)
WADE_CAM = {"x": 79.5, "y": 18.3, "z": 15.0, "yaw": 180.0, "pitch": -14.0}
NPC = "sweep_wader"


def clear_volumes(api):
    for v in api.debug("water_av_list").get("volumes", []):
        api.debug("water_av_destroy", {"id": v["id"], "discard": True})


def wade_take(api, ff, a, mode):
    api.debug("water_coupling", {"characters": mode == "on"})
    api.post("/api/npc/remove", {"name": NPC})
    clear_volumes(api)
    r = api.debug("water_av_create", {**CH_BOX, "cellSize": 1.0 / 9.0, "transport": "eulerian", "backend": "auto", "auto_sleep": False})
    if "error" in r: raise SystemExit(f"create: {r}")
    api.debug("place_water_box", {"x1": 66, "y1": 16, "z1": 14, "x2": 77, "y2": 16, "z2": 15, "mass": DEPTH, "target": "core"})
    api.debug("water_av_realtime", {"on": True})
    m0 = api.debug("water_av_list").get("total_mass")
    camera_set(api, WADE_CAM)
    time.sleep(4.0)   # the water at rest before the take
    r = api.post("/api/npc/spawn", {"name": NPC, "position": {"x": 66.8, "y": 16.0, "z": LANE_Z}, "behavior": "patrol", "walkSpeed": 2.0,
                                    "waitTime": 0.5, "waypoints": [{"x": 66.8, "y": 16.0, "z": LANE_Z}, {"x": 76.4, "y": 16.0, "z": LANE_Z}]})
    if not r.get("success", True) or "error" in r: raise SystemExit(f"npc spawn: {r}")
    raw = EV / f"{a.tag}_{mode}_raw.mp4"
    rec = record(ff, a.window, a.seconds, raw)
    t0 = time.time(); rows = []
    while rec.poll() is None:   # the frame rate only (no water reads while recording - D9)
        rows.append({"t": round(time.time() - t0, 2), "fps": fps(api)})
        time.sleep(0.5)
    api.post("/api/npc/remove", {"name": NPC})
    time.sleep(3.0)
    m1 = api.debug("water_av_list").get("total_mass")
    c = api.debug("water_coupling", {}).get("characters", {})
    label = ("CHARACTERS OFF - the wader is not fed to the water (before A4): the surface stays flat" if mode == "off" else
             "CHARACTERS ON (shipped) - the wader pushes a bow wave and leaves a wake")
    fl = [r["fps"] for r in rows if r["fps"]]
    srt = EV / f"{a.tag}_{mode}.srt"
    write_srt(srt, [(0.0, f"{label}\nan NPC patrols a 0.8 m deep channel (x 66-77) | {min(fl or [0]):.0f}-{max(fl or [0]):.0f} fps")],
              a.seconds)
    caption(ff, raw, srt, EV / f"{a.tag}_{mode}.mp4", a.crop)
    raw.unlink(missing_ok=True)
    return {"mode": mode, "mass": [m0, m1], "coupling_characters": c, "fps": rows}


def item_wade(api, ff, a):
    out = {"bench": "WaterBench_Small", "box": CH_BOX, "depth": DEPTH, "camera": WADE_CAM, "takes": []}
    try:
        for mode in ("off", "on"):
            out["takes"].append(wade_take(api, ff, a, mode))
    finally:
        api.post("/api/npc/remove", {"name": NPC})
        api.debug("water_coupling", {"characters": True})
        clear_volumes(api)
    out["restored"] = api.debug("water_coupling", {}).get("characters")
    subprocess.run([ff, "-y", "-loglevel", "error", "-i", f"{a.tag}_off.mp4", "-i", f"{a.tag}_on.mp4", "-filter_complex",
                    "[0:v][1:v]hstack=inputs=2[v]", "-map", "[v]", "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "21",
                    f"{a.tag}_side_by_side.mp4"], check=True, cwd=str(EV))
    (EV / f"{a.tag}.json").write_text(json.dumps(out, indent=1))
    print(json.dumps({t["mode"]: t["mass"] for t in out["takes"]}))
    print("wrote", EV / f"{a.tag}_side_by_side.mp4")


# ---------------------------------------------------------------- A2i scenes
# above the grass of the dry scene (a flat Grass world: at 1.4 m its blades fill the frame), the pond still in view
# 58 item 3 (the owner on the first cut: "i dont see waves or splashes and part way the camera changes to random location"): ~5 m
# from the pond, 3 m over it (was ~9 m up and 9 m back: a 1/3 m block's splash was a few pixels)
POND_CAM = {"x": 102.0, "y": 20.5, "z": 19.0, "yaw": -90.0, "pitch": -32.0}
PLAYER_AWAY = {"x": 92.0, "y": 17.0, "z": 30.0}   # a scene load spawns the player under the camera: moved out of the shot
DROPS = [(101.2, 13.2), (102.8, 14.0)]
STONE = (102.0, 13.6)   # 58 item 3: a stone dropped from higher beside the wood - a splash that reads (the wood floats, the stone sinks)


def scene_go(api, sid, limit=60):
    r = api.post("/api/scene/transition", {"scene_id": sid})
    if not r.get("success"): raise SystemExit(f"transition {sid}: {r}")
    t0 = time.time()
    while time.time() - t0 < limit:
        # 58 item 3: the camera held on the pond every 50 ms while the scene loads - a load places the camera at the scene's own
        # pose (the first cut's "camera changes to random location" was that pose, shown until this loop ended)
        try: camera_set(api, POND_CAM)
        except Exception: pass
        s = api.get("/api/scene/active")
        if s.get("id", s.get("scene_id")) == sid and not s.get("transitioning", False): break
        time.sleep(0.05)
    api.post("/api/entity/move", {"id": "player", "position": PLAYER_AWAY})
    camera_set(api, POND_CAM)


def water_state(api):
    c = api.debug("water_coupling")
    v = api.debug("water_av_list")
    try: debris = api.get("/api/debug/engine_timing").get("gpuActive")
    except Exception: debris = None
    return {"volumes": len(v.get("volumes", [])), "mass": v.get("total_mass"), "slept_boxes": (c.get("slept") or {}).get("boxes"),
            "debris": debris, "world_last": (c.get("world") or {}).get("last")}


def drop(api, y):
    for (x, z) in DROPS:
        api.debug("spawn_gpu_particle", {"x": x, "y": y, "z": z, "material": "Wood", "scale": 1.0 / 3.0, "lifetime": 60.0, "spin": 0.0})
    api.debug("spawn_gpu_particle", {"x": STONE[0], "y": y + 2.0, "z": STONE[1], "material": "Stone", "scale": 1.0 / 3.0, "lifetime": 60.0, "spin": 0.0})


def item_scenes(api, ff, a):
    s = api.get("/api/scene/active")
    if s.get("id", s.get("scene_id")) != "pond": scene_go(api, "pond")
    api.post("/api/entity/move", {"id": "player", "position": PLAYER_AWAY})
    camera_set(api, POND_CAM); time.sleep(4.0)
    ev = {"start": water_state(api)}
    raw = EV / f"{a.tag}_raw.mp4"
    rec = record(ff, a.window, a.seconds, raw)
    t0 = time.time(); cues = []
    def at(): return round(time.time() - t0, 2)
    cues.append((0.0, "scene 'pond': a 4 x 4 m pond, asleep (drawn from the record)"))
    time.sleep(3.0)
    drop(api, 19.5); cues.append((at(), "wood and a stone dropped - the pond wakes, splashes, the wood floats, the stone sinks"))
    time.sleep(6.0)
    ev["pond_awake"] = water_state(api)
    cues.append((at(), f"scene change to 'dry' with the pond AWAKE ({ev['pond_awake']['volumes']} live volume)"))
    scene_go(api, "dry"); time.sleep(1.0)
    ev["dry"] = water_state(api)
    cues.append((at(), f"scene 'dry' (same place, no water): {ev['dry']['volumes']} live volumes - no water followed\n"
                       f"GPU debris live here: {ev['dry']['debris']} (the wood - debris crossing a scene is NOT the water's: logged)"))
    time.sleep(5.0)
    cues.append((at(), "scene change back to 'pond'"))
    scene_go(api, "pond"); time.sleep(1.0)
    ev["pond_back"] = water_state(api)
    cues.append((at(), f"back in 'pond': the pond is there once, asleep ({ev['pond_back']['volumes']} live volumes, "
                       f"{len(ev['pond_back']['slept_boxes'] or [])} slept box) - and nothing of 'dry' left in it (58 item 3 fix)"))
    time.sleep(4.0)
    drop(api, 19.5); cues.append((at(), "wood and a stone dropped again - it wakes and splashes as before"))
    while rec.poll() is None: time.sleep(0.2)
    ev["end"] = water_state(api)
    srt = EV / f"{a.tag}.srt"
    write_srt(srt, cues, a.seconds)
    caption(ff, raw, srt, EV / f"{a.tag}.mp4", a.crop)
    (EV / f"{a.tag}.json").write_text(json.dumps({"bench": "WaterBench_Scenes", "camera": POND_CAM, "drops": DROPS, "cues": cues,
                                                  "events": ev}, indent=1))
    print(json.dumps({k: {kk: vv for kk, vv in v.items() if kk != "world_last"} for k, v in ev.items()}))
    print("wrote", EV / f"{a.tag}.mp4")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--item", required=True, choices=["wade", "scenes"])
    ap.add_argument("--url")
    ap.add_argument("--window")
    ap.add_argument("--tag")
    ap.add_argument("--seconds", type=float)
    ap.add_argument("--crop", default="")
    a = ap.parse_args()
    d = {"wade": ("http://127.0.0.1:8111", "WaterBench_Small", "sweep_a4_wade", 14.0),
         "scenes": ("http://127.0.0.1:8112", "WaterBench_Scenes", "sweep_a2i_scenes", 34.0)}[a.item]
    a.url = a.url or d[0]; a.window = a.window or d[1]; a.tag = a.tag or d[2]; a.seconds = a.seconds or d[3]
    ff = shutil.which("ffmpeg")
    if not ff: raise SystemExit("ffmpeg not on PATH")
    api = Api(a.url); api.wait_status(600)
    try: api.debug("editor_panels", {"item_equipper": False, "tool_panels": False})
    except Exception as e: print("editor_panels:", e)
    (item_wade if a.item == "wade" else item_scenes)(api, ff, a)


if __name__ == "__main__":
    main()
