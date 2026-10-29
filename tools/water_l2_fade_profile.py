"""WaterCore L2 (43.3 item 6) - the ripples' fade by distance, read off the screen.

WaterBench_Creek (port 8114, Release). The 22.11 `pattern` pins every ripple layer to one uniform tilt (slope 0.3, no stones, no
step), and `water_render_core {debug: 9}` draws the ripple slope AFTER L2's fade as a grey (0..1 = slope 0..0.5), tone map curve 0
(the passthrough: every capture passes the tone map). The camera is fixed straight down over one point P of the water; only the
WATER's viewer moves (water_locality test_viewer), west of P along z = P.z, 0..--max-d m away in 0.5 m steps. Each step one window
frame (gdigrab: /api/screenshot omits pond water) and the mean grey of a 9 x 9 px patch at P's pixel. One variable: the distance.

Rigs:
  pond  a 28 x 28 m pond inside ONE chunk (x 4-31, z 2-29), P = (18, 16): it fits the window, so no window edge
  lake  the 60 x 28 m lake of 27.24 (x 4-63, two chunks wide), P = (55, 16): larger than the window - the window-edge control
Conditions: reach on (ripple_m 21, fade 8) and off (the control, exactly before L2).
Predictions (written before the first run, 2026-10-29):
  pond on   the grey / its near value follows detail(d, 13, 21) within one grey level (1/255 of full), 0 from 21 m
  pond off  flat (1.0) to the far end of the sweep
  lake off  1.0 until P leaves the 384-cell window (d ~ 17-25 m, by where the window last moved), then 0 in ONE step
  lake on   as pond on: the fade is over before the window could end
The grey's transfer is calibrated, not assumed: at d = 0 a second frame at slope 0.15 gives the ratio a linear output would put
at 0.5 (an sRGB-encoded one at ~0.73); the fade is read through that.

    python tools/water_l2_fade_profile.py [--rig pond|lake|both] [--max-d 30] [--tag l2_fade]
Writes docs/evidence/water_core_m4/<tag>_<rig>.json and one still per condition (<tag>_<rig>_<cond>_d0.png).
"""
import argparse, json, math, pathlib, shutil, subprocess, sys, tempfile, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set, pose_ok, world_job

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_m4"
H, TOP = 1.0 / 9.0, 16
RIGS = {"pond": {"box": (4, 2, 31, 29), "p": (18.0, 16.0)}, "lake": {"box": (4, 2, 63, 29), "p": (55.0, 16.0)}}
SLOPE = 0.3


def detail(d, n, f):
    if d <= n: return 1.0
    if d >= f: return 0.0
    t = (d - n) / (f - n)
    return 1.0 - t * t * (3.0 - 2.0 * t)


def destroy_all(api):
    for v in api.debug("water_av_list").get("volumes", []): api.debug("water_av_destroy", {"id": v["id"], "discard": True})


def make(api, box):
    api.debug("clear_dynamics"); destroy_all(api)
    x1, z1, x2, z2 = box
    b = {"x1": x1, "y1": TOP, "z1": z1, "x2": x2, "y2": TOP + 2, "z2": z2}
    r = api.debug("water_av_create", {**b, "cellSize": H, "backend": "gpu", "auto_sleep": False})
    if "error" in r: raise SystemExit("create refused: " + r["error"])
    for k in range(2): api.debug("place_water_box", {"target": "core", **b, "y1": TOP + k, "y2": TOP + k, "mass": 1.0})
    time.sleep(8.0)


def grab(ff, window, path):
    subprocess.run([ff, "-y", "-loglevel", "error", "-f", "gdigrab", "-i", f"title={window}", "-frames:v", "1", str(path)], check=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8114)
    ap.add_argument("--window", default="WaterBench_Creek")
    ap.add_argument("--rig", default="both", choices=["pond", "lake", "both"])
    ap.add_argument("--max-d", type=float, default=30.0)
    ap.add_argument("--tag", default="l2_fade")
    ap.add_argument("--settle", type=float, default=0.25, help="seconds after each viewer move before the grab")
    ap.add_argument("--side", default="west", choices=["west", "east"], help="the viewer walks away from P to the west (-x) or east")
    ap.add_argument("--conds", default="on,off")
    ap.add_argument("--look-capture", default="", help="43.3 item 4: no sweep - the pond, the pattern tilt, normal shading, one frame "
                    "from a fixed oblique pose, saved as l1_px_<tag>.png for `water_l1_pixels.py compare` (the reach as the engine has it)")
    a = ap.parse_args()
    import numpy as np
    from PIL import Image
    ff = shutil.which("ffmpeg")
    if not ff: raise SystemExit("ffmpeg not on PATH")
    api = Api(f"http://127.0.0.1:{a.port}"); st = api.wait_status(300)
    try: api.debug("editor_panels", {"item_equipper": False, "tool_panels": False})
    except Exception as e: print("editor_panels:", e)
    loc0 = api.debug("water_locality", {})
    api.debug("water_av_realtime", {"on": True})
    api.debug("water_ripples", {"enabled": True, "shape": False})
    tmp = pathlib.Path(tempfile.mkdtemp())
    rigs = ["pond", "lake"] if a.rig == "both" else [a.rig]
    tm0 = {}
    if a.look_capture:   # works on a build before L2 too (the L2 fields are ignored there)
        try:
            # a sand bed (the grass blades under the water sway in the wind: 7.7 % of pixels moved between two captures)
            x1, z1, x2, z2 = RIGS["pond"]["box"]
            bed = {"x1": x1, "y1": TOP - 1, "z1": z1, "x2": x2, "y2": TOP - 1, "z2": z2}
            world_job(api, "/api/world/clear", bed); world_job(api, "/api/world/fill", bed, material="Sand")
            make(api, RIGS["pond"]["box"])
            pose = {"x": 18.0, "y": TOP + 2 + 4.0, "z": -2.0, "yaw": 90.0, "pitch": -24.0}
            got = camera_set(api, pose)
            if not pose_ok(pose, got): raise SystemExit(f"camera pose not taken: {got}")
            api.debug("water_ripples", {"pattern": {"slope": SLOPE, "azimuth_deg": 30.0}})
            time.sleep(3.0)
            out = EV / f"l1_px_{a.look_capture}.png"
            subprocess.run([ff, "-y", "-loglevel", "error", "-f", "gdigrab", "-i", f"title={a.window}", "-frames:v", "1", "-vf", "crop=954:600:243:50", str(out)], check=True)
            meta = {"tag": a.look_capture, "pose": got, "build": st.get("build_config"), "locality": api.debug("water_locality", {}), "ripples": api.debug("water_ripples", {})}
            (EV / f"l1_px_{a.look_capture}.json").write_text(json.dumps(meta, indent=1), encoding="utf-8")
            print("wrote", out)
        finally:
            api.debug("water_ripples", {"pattern": False})
            api.debug("clear_dynamics"); destroy_all(api)
        return
    try:
        tm0 = api.post("/api/debug/tonemap", {})
        api.post("/api/debug/tonemap", {"curve": 0, "exposure": 1.0})   # the passthrough, unscaled (x8 would saturate the grey)
        for rig in rigs:
            R = RIGS[rig]
            make(api, R["box"])
            px, pz = R["p"]
            pose = {"x": px, "y": TOP + 2 + 9.0, "z": pz, "yaw": 0.0, "pitch": -89.0}
            got = camera_set(api, pose)
            if not pose_ok(pose, got): raise SystemExit(f"camera pose not taken: {got}")
            api.debug("water_ripples", {"pattern": {"slope": SLOPE, "azimuth_deg": 30.0}, "shape": False})
            api.debug("water_render_core", {"debug": 9})
            out = {"rig": rig, "box": R["box"], "p": R["p"], "pose": got, "build": st.get("build_config"), "conds": {}}
            sgn = -1.0 if a.side == "west" else 1.0
            out["side"], out["settle_s"] = a.side, a.settle
            for cond in a.conds.split(","):
                api.debug("water_locality", {"ripple_reach": cond == "on", "ripple_m": 21.0, "ripple_fade_m": 8.0, "test_viewer": [px, 17.0, pz]})
                time.sleep(1.0)
                # find P's pixel: the patch of the uniform debug grey around the frame's centre (the camera is straight over P)
                f0 = tmp / "f0.png"; grab(ff, a.window, f0)
                img = np.asarray(Image.open(f0).convert("RGB")).astype(np.int16)
                shutil.copy(f0, EV / f"{a.tag}_{rig}_{cond}_d0.png")
                hgt, wid = img.shape[:2]
                cy, cx = hgt // 2 + 15, wid // 2   # the window's title bar (~30 px) shifts the client centre down
                def patch(im):
                    p = im[cy - 4:cy + 5, cx - 4:cx + 5]
                    return float(p.mean()), float(np.abs(p[..., 0] - p[..., 1]).max() + np.abs(p[..., 1] - p[..., 2]).max())
                full, chroma = patch(img)
                if chroma > 6: print(f"  warning: the patch is not grey (chroma {chroma}) - not on the debug water?")
                # the transfer: half the slope at full weight
                api.debug("water_ripples", {"pattern": {"slope": SLOPE / 2, "azimuth_deg": 30.0}, "shape": False}); time.sleep(0.5)
                grab(ff, a.window, f0); half = patch(np.asarray(Image.open(f0).convert("RGB")).astype(np.int16))[0]
                api.debug("water_ripples", {"pattern": {"slope": SLOPE, "azimuth_deg": 30.0}, "shape": False}); time.sleep(0.5)
                ratio = half / full if full > 0 else None
                srgb = ratio is not None and ratio > 0.62
                to_lin = (lambda g: ((g / 255.0 + 0.055) / 1.055) ** 2.4 if g / 255.0 > 0.04045 else g / 255.0 / 12.92) if srgb else (lambda g: g / 255.0)
                rows = []
                n = int(round(a.max_d / 0.5)) + 1
                for i in range(n):
                    d = 0.5 * i
                    api.debug("water_locality", {"test_viewer": [px + sgn * d, 17.0, pz]})
                    time.sleep(a.settle)
                    grab(ff, a.window, f0)
                    g, ch = patch(np.asarray(Image.open(f0).convert("RGB")).astype(np.int16))
                    w = to_lin(g) / to_lin(full) if full > 0 else None
                    rows.append({"d": d, "grey": round(g, 2), "weight": round(w, 4) if w is not None else None, "chroma": ch,
                                 "predicted": round(detail(d, 13.0, 21.0), 4) if cond == "on" else 1.0})
                rp = api.debug("water_ripples", {"shape": False})
                wsteps = [abs(rows[i + 1]["weight"] - rows[i]["weight"]) for i in range(len(rows) - 1)]
                err = [abs(r["weight"] - r["predicted"]) for r in rows]
                out["conds"][cond] = {"full_grey": full, "half_grey": half, "ratio_half": ratio, "transfer": "srgb" if srgb else "linear",
                                      "max_abs_err_vs_prediction": round(max(err), 4), "largest_step": round(max(wsteps), 4),
                                      "largest_step_at_m": rows[wsteps.index(max(wsteps)) + 1]["d"], "window_side": rp.get("window_side"),
                                      "slots": rp.get("slot_list"), "rows": rows}
                print(f"{rig} {cond}: full grey {full:.1f}, half {half:.1f} ({'srgb' if srgb else 'linear'}); largest step "
                      f"{max(wsteps):.3f} at {out['conds'][cond]['largest_step_at_m']} m; max |weight - prediction| {max(err):.3f}", flush=True)
                print("   d: " + " ".join(f"{r['d']:.0f}:{r['weight']:.2f}" for r in rows if r["d"] == int(r["d"])), flush=True)
            (EV / f"{a.tag}_{rig}{'' if a.side == 'west' else '_east'}.json").write_text(json.dumps(out, indent=1), encoding="utf-8")
    finally:
        api.debug("water_render_core", {"debug": 0})
        api.debug("water_ripples", {"pattern": False, "shape": False})
        api.post("/api/debug/tonemap", {"curve": tm0.get("curve", 1), "exposure": tm0.get("exposure", 8.0)})
        api.debug("water_locality", {"clear_test_viewer": True, "ripple_reach": loc0.get("ripple_reach", False),
                                     "ripple_m": loc0.get("ripple_m", 21.0), "ripple_fade_m": loc0.get("ripple_fade_m", 8.0)})
        api.debug("clear_dynamics"); destroy_all(api)


if __name__ == "__main__":
    main()
