"""Measure exactly when a ripple facet renders near-black or near-white (WaterCore.md 22.11).

Part A - WHAT: the ripple layer is pinned to one uniform facet tilt (water_ripples {pattern}), so the whole pond
shows a single slope s toward a single azimuth. Sweeping s x azimuth x camera pitch and reading the pond's pixels
from the engine's own screenshot (the owner's view: exposure + AgX) gives the slope at which water turns black or
white, and the reflected-ray geometry at that point (computed from the shader's formulas at the pond centre).

Part B - WHEN: five 1/3 m stones dropped together (droplets OFF, so spray cubes are not counted as white), the
live facet-slope histogram polled at ~20 Hz and the screen recorded; the per-frame share of pond pixels that are
near-black / near-white is read off the recording. Control: the same drop with ripples off.

Small bench (port 8111). Usage: python tools/water_facet_extremes.py [--part A|B|AB] [--quick]
"""
import argparse, json, math, shutil, subprocess, sys, time
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from water_bench import Api, camera_set  # noqa: E402
from water_splash_demo import build_pond, POSES  # noqa: E402

EV = Path(__file__).resolve().parents[1] / "docs" / "evidence" / "water_core_e"
POND_C = np.array([101.5, 16.5, 13.5])
ALL_POSES = dict(POSES, top={"x": 104.0, "y": 22.0, "z": 13.5, "yaw": 180, "pitch": -62})
SLOPES = [0.0, 0.05, 0.1, 0.15, 0.2, 0.25, 0.3, 0.4, 0.5, 0.6, 0.75, 1.0, 1.25, 1.5, 2.0, 3.0, 5.0]
AZ = {0: "away", 45: "away-right", 90: "right", 135: "toward-right", 180: "toward", 225: "toward-left", 270: "left", 315: "away-left"}
BLACK, WHITE = 30, 225   # 8-bit display values: max channel below / min channel above


def shot(api):
    r = api.get("/api/screenshot", timeout=90)
    p = r.get("path") or next((s.get("path") for s in r.get("screenshots", [])), None)
    return np.asarray(Image.open(p).convert("RGB")).astype(np.int16)


def classify(img, mask):
    px = img[mask]
    mx, mn = px.max(axis=1), px.min(axis=1)
    luma = (0.2126 * px[:, 0] + 0.7152 * px[:, 1] + 0.0722 * px[:, 2])
    return {"black": float((mx < BLACK).mean()), "white": float((mn > WHITE).mean()), "luma": float(luma.mean()),
            "luma_p05": float(np.percentile(luma, 5)), "luma_p95": float(np.percentile(luma, 95)),
            "rgb": [round(float(c), 1) for c in px.mean(axis=0)]}


def erode(m, k=3):
    out = m.copy()
    for dy in range(-k, k + 1):
        for dx in range(-k, k + 1):
            out &= np.roll(np.roll(m, dy, 0), dx, 1)
    return out


def pond_mask_from(img_normals, img_off):
    """Top-face water pixels: the normals tap (n ~ +y -> (0.5, 1, 0.5), near-white after exposure x8 + AgX) differs from
    the scene drawn without water. Side faces (n horizontal) are darker in the tap and drop out."""
    diff = np.abs(img_normals - img_off).sum(axis=2) > 60
    top = img_normals.min(axis=2) > 200
    return erode(diff & top, 3)


def geometry(pose, s, az_deg, to_sun):
    """The shader's reflection at the pond centre (water_common.glsl shadeWaterSurface, ripple tilt of 22.4)."""
    cam = np.array([pose["x"], pose["y"], pose["z"]])
    a = math.radians(az_deg)
    n = np.array([-s * math.cos(a), 1.0, -s * math.sin(a)]); n /= np.linalg.norm(n)
    v = cam - POND_C; v /= np.linalg.norm(v)
    vn = float(v @ n)
    r = -v + 2.0 * vn * n
    out = {"tilt_deg": round(math.degrees(math.atan(s)), 1), "v_dot_n": round(vn, 3),
           "ndv": round(max(vn, 0.0), 3), "fresnel": round(0.02 + 0.98 * (1.0 - max(min(vn, 1.0), 0.0)) ** 5, 3),
           "reflect_elev_deg": round(math.degrees(math.asin(max(-1.0, min(1.0, r[1])))), 1)}
    if to_sun is not None:
        hv = to_sun + v; hv /= np.linalg.norm(hv)
        out["n_dot_h"] = round(float(n @ hv), 4)
        out["reflect_to_sun_deg"] = round(math.degrees(math.acos(max(-1.0, min(1.0, float(r @ to_sun))))), 1)
    return out


# The sun the renderer uses while the day/night cycle is OFF (RenderCoordinator.h `sunDirection`, the direction the
# light travels). /api/daynight reports the cycle's own (unused) noon sun then - it is not what is drawn.
FIXED_SUN_TRAVEL = (-0.6, -0.7, -0.45)


def sun_dir(api):
    try:
        d = api.get("/api/daynight")
    except Exception:
        return None, None
    if not d.get("enabled", True):
        t = -np.array(FIXED_SUN_TRAVEL); return t / np.linalg.norm(t), d
    for k in ("sunDirection", "sun_direction", "sunDir"):
        v = d.get(k) or d.get("daynight", {}).get(k)
        if isinstance(v, dict): v = [v.get("x"), v.get("y"), v.get("z")]
        if isinstance(v, list) and len(v) == 3:
            t = -np.array(v, dtype=float); return t / np.linalg.norm(t), d
    return None, d


def part_a(api, quick):
    build_pond(api)
    api.debug("water_droplets", {"enabled": False})
    api.debug("water_ripples", {"enabled": True, "pattern": {"slope": 0.0, "azimuth_deg": 0.0}})
    time.sleep(4)
    to_sun, dn = sun_dir(api)
    slopes = SLOPES[::2] + [SLOPES[-1]] if quick else SLOPES
    az_list = [0, 90, 180, 270] if quick else list(AZ)
    rows, masks = [], {}
    for pname in (["mid"] if quick else ["low", "mid", "high", "top"]):
        pose = ALL_POSES[pname]
        got = camera_set(api, pose)
        api.debug("water_render_core", {"mode": "mesh", "debug": 1}); time.sleep(0.4)
        nrm = shot(api)
        api.debug("water_render_core", {"mode": "off", "debug": 0}); time.sleep(0.4)
        off = shot(api)
        api.debug("water_render_core", {"mode": "mesh", "debug": 0}); time.sleep(0.4)
        mask = pond_mask_from(nrm, off)
        masks[pname] = int(mask.sum())
        Image.fromarray((mask * 255).astype(np.uint8)).save(EV / f"facet_extremes_mask_{pname}.png")
        print(f"pose {pname} (pitch {pose['pitch']}): {mask.sum()} pond pixels, camera {got}")
        if mask.sum() < 500:
            print("  too few pond pixels - skipped"); continue
        for az in az_list:
            for s in slopes:
                api.debug("water_ripples", {"pattern": {"slope": s, "azimuth_deg": az}}); time.sleep(0.25)
                c = classify(shot(api), mask)
                g = geometry(pose, s, az, to_sun)
                rows.append({"pose": pname, "pitch": pose["pitch"], "azimuth_deg": az, "facing": AZ[az], "slope": s, **g, **c})
                print(f"  {pname:4} {AZ[az]:12} s {s:4.2f} tilt {g['tilt_deg']:5.1f}  R elev {g['reflect_elev_deg']:6.1f}"
                      f"  luma {c['luma']:6.1f}  black {c['black']*100:5.1f}%  white {c['white']*100:5.1f}%")
    api.debug("water_ripples", {"pattern": False})
    return {"sun_to": None if to_sun is None else [round(float(x), 4) for x in to_sun], "daynight": dn,
            "thresholds": {"black_max_channel_below": BLACK, "white_min_channel_above": WHITE},
            "mask_pixels": masks, "rows": rows}


def srgb_to_linear(c):
    c = np.asarray(c, dtype=float)
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def part_p(api, quick):
    """Per pixel: the shader's own reflected ray (debug tap 8, no tone curve) joined with the owner's frame."""
    build_pond(api)
    api.debug("water_droplets", {"enabled": False})
    api.debug("water_ripples", {"enabled": True, "pattern": {"slope": 0.0, "azimuth_deg": 0.0}})
    time.sleep(4)
    grade = api.post("/api/debug/tonemap", {})
    print("grade", grade)
    slopes = [0.2, 0.5, 1.0] if quick else [0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.75, 1.0, 1.25, 1.5, 2.0]
    elev_bins = np.zeros((181, 3)); sun_bins = np.zeros((181, 3))   # per degree: pixels, black, white
    enc = []
    for pname in (["mid"] if quick else ["low", "mid", "high", "top"]):
        camera_set(api, ALL_POSES[pname])
        api.debug("water_render_core", {"mode": "mesh", "debug": 1}); time.sleep(0.4); nrm = shot(api)
        api.debug("water_render_core", {"mode": "off", "debug": 0}); time.sleep(0.4); off = shot(api)
        api.debug("water_render_core", {"mode": "mesh", "debug": 0})
        mask = pond_mask_from(nrm, off)
        if mask.sum() < 500: continue
        for az in ([0, 90, 180, 270] if quick else list(AZ)):
            for s in slopes:
                api.debug("water_ripples", {"pattern": {"slope": s, "azimuth_deg": az}})
                api.debug("water_render_core", {"debug": 0}); time.sleep(0.3)
                fin = shot(api)[mask]
                api.post("/api/debug/tonemap", {"curve": 0, "exposure": 1.0, "bloom": False})
                api.debug("water_render_core", {"debug": 8}); time.sleep(0.3)
                tap = shot(api)[mask]
                api.post("/api/debug/tonemap", {"curve": grade.get("curve", 1), "exposure": grade.get("exposure", 8.0),
                                                "bloom": grade.get("bloom", True)})
                enc.append(float(tap[:, 2].mean()))
                lin = srgb_to_linear(tap / 255.0)   # the swapchain is sRGB: the tap's 0.5 reads 188 (checked per capture)
                to_sun = np.clip(np.rint(lin[:, 0] * 180.0), 0, 180).astype(int)
                elev = np.clip(np.rint(lin[:, 1] * 180.0), 0, 180).astype(int)   # index = elevation + 90
                blk = fin.max(axis=1) < BLACK; wht = fin.min(axis=1) > WHITE
                for bins, idx in ((elev_bins, elev), (sun_bins, to_sun)):
                    np.add.at(bins[:, 0], idx, 1); np.add.at(bins[:, 1], idx, blk); np.add.at(bins[:, 2], idx, wht)
            print(f"  {pname} {AZ[az]} done")
    api.debug("water_render_core", {"debug": 0})
    api.debug("water_ripples", {"pattern": False})
    def table(bins, lo):
        return [{"deg": i + lo, "pixels": int(b[0]), "black": round(b[1] / b[0], 4), "white": round(b[2] / b[0], 4)}
                for i, b in enumerate(bins) if b[0] > 0]
    return {"encoding_check_blue_mean": round(float(np.mean(enc)), 1) if enc else None,
            "note": "tap 8 blue = 0.5 reads ~188: the frame is sRGB-encoded; angles are decoded through sRGB -> linear (8-bit: ~0.3 deg steps near 90, ~2-4 deg near 0)",
            "by_reflect_elevation": table(elev_bins, -90), "by_angle_to_sun": table(sun_bins, 0)}


def part_b(api, ripples, seconds=12.0):
    ff = shutil.which("ffmpeg")
    if not ff: raise SystemExit("ffmpeg not on PATH")
    tag = "facet_extremes_live" + ("" if ripples else "_noripple")
    build_pond(api)
    api.debug("water_droplets", {"enabled": False})
    api.debug("water_ripples", {"enabled": ripples, "pattern": False})
    camera_set(api, POSES["mid"]); time.sleep(6)

    def clip(name, secs=1.0):
        mp4 = EV / f"{tag}_{name}.mp4"
        subprocess.run([ff, "-y", "-loglevel", "error", "-f", "gdigrab", "-framerate", "30", "-t", str(secs),
                        "-i", "title=WaterBench_Small", "-c:v", "libx264", "-qp", "0", str(mp4)], check=True)
        return mp4

    def frames(mp4, fps=None):
        d = mp4.with_suffix(""); d.mkdir(exist_ok=True)
        for f in d.glob("*.png"): f.unlink()
        vf = ["-vf", f"fps={fps}"] if fps else []
        subprocess.run([ff, "-y", "-loglevel", "error", "-i", str(mp4), *vf, str(d / "f_%04d.png")], check=True)
        return sorted(d.glob("*.png"))

    def load(p): return np.asarray(Image.open(p).convert("RGB")).astype(np.int16)
    api.debug("water_render_core", {"mode": "mesh", "debug": 1}); time.sleep(0.4)
    nrm = load(frames(clip("normals", 0.5))[-1])
    api.debug("water_render_core", {"mode": "off", "debug": 0}); time.sleep(0.4)
    off = load(frames(clip("off", 0.5))[-1])
    api.debug("water_render_core", {"mode": "mesh", "debug": 0}); time.sleep(1.0)
    mask = pond_mask_from(nrm, off)
    print(f"live mask: {mask.sum()} pond pixels")
    mp4 = EV / f"{tag}.mp4"
    rec = subprocess.Popen([ff, "-y", "-loglevel", "error", "-f", "gdigrab", "-framerate", "30", "-t", str(seconds),
                            "-i", "title=WaterBench_Small", "-c:v", "libx264", "-qp", "0", str(mp4)])
    t_rec = time.time()
    time.sleep(1.0)
    t_drop = time.time() - t_rec
    for x, z in [(100.6, 12.6), (102.4, 12.7), (101.5, 13.5), (100.7, 14.4), (102.5, 14.3)]:
        api.debug("spawn_gpu_particle", {"x": x, "y": 20.0, "z": z, "material": "Stone", "scale": 1.0 / 3.0, "lifetime": 60.0})
    polls = []
    while rec.poll() is None:
        r = api.debug("water_ripples", {})
        polls.append({"t": round(time.time() - t_rec, 3), "max_abs_m": r.get("max_abs_m"), "max_slope": r.get("max_slope"),
                      "hist": r.get("slope_hist")})
        time.sleep(0.05)
    edges = r.get("slope_edges")
    per = []
    for i, f in enumerate(frames(mp4)):
        c = classify(load(f), mask)
        per.append({"t": round(i / 30.0, 3), "black": c["black"], "white": c["white"], "luma": c["luma"]})
    return {"ripples": ripples, "video": str(mp4), "drop_t": round(t_drop, 2), "mask_pixels": int(mask.sum()),
            "slope_edges": edges, "polls": polls, "frames": per}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8111")
    ap.add_argument("--part", default="AB")
    ap.add_argument("--quick", action="store_true")
    args = ap.parse_args()
    api = Api(args.url); api.wait_status(60)
    out = {"captured": time.strftime("%Y-%m-%d %H:%M:%S")}
    if "A" in args.part: out["A"] = part_a(api, args.quick)
    if "P" in args.part: out["P"] = part_p(api, args.quick)
    if "B" in args.part:
        out["B_ripples"] = part_b(api, True)
        out["B_control"] = part_b(api, False)
    p = EV / ("facet_extremes_quick.json" if args.quick else ("facet_extremes_pixels.json" if args.part == "P" else "facet_extremes.json"))
    p.write_text(json.dumps(out, indent=1))
    print("wrote", p)


if __name__ == "__main__":
    main()
