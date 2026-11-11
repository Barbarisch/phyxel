"""WaterCore.md 51.4 S3 live (L4): the far skin on the 150 x 150 m lake written straight into the record, seen from the clip's WIDE pose
at the DEFAULT record radius (draw_m 48 m - 51.3c drew the far half as sand there). Skin off, then on, at rest: a still each (gdigrab -
/api/screenshot omits pond water), the whole frame (water_a3_cost.window), the skin's stats; the pixels the skin changed. Then, skin on,
the same visible splash as 51.3b (2 m / strength 8 at the centre): the whole frame 0-8 s after it.

The rig: WaterBench_Creek (port 8114, Release), the bed + kerb of water_record_lake_clip.py; engine defaults except the skin (ON for
the "on" half - it ships OFF).

    python tools/water_skin_live.py [--tag skin150]
Writes docs/evidence/water_core_m4/<tag>_off.png, <tag>_on.png, <tag>_diff.png and <tag>.json.
"""
import argparse, json, pathlib, shutil, subprocess, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set
from water_a3_cost import window
from water_record_lake_clip import EV, POSES, lay_lake, splash, destroy_all


def grab(ff, window_title, crop, png):
    subprocess.run([ff, "-y", "-loglevel", "error", "-f", "gdigrab", "-i", f"title={window_title}", "-frames:v", "1", "-vf", f"crop={crop}", str(png)], check=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8114")
    ap.add_argument("--window", default="WaterBench_Creek")
    ap.add_argument("--crop", default="954:560:243:60")
    ap.add_argument("--pose", default="wide")
    ap.add_argument("--tag", default="skin150")
    ap.add_argument("--rest-s", type=float, default=6.0)
    ap.add_argument("--splash-ab", action="store_true", help="51.6: three splashes, the skin on / off / on, 30 s rest after each")
    a = ap.parse_args()
    ff = shutil.which("ffmpeg")
    api = Api(a.url); api.wait_status(300)
    try: api.debug("editor_panels", {"item_equipper": False, "tool_panels": False})
    except Exception as e: print("editor_panels:", e)
    api.debug("water_av_realtime", {"on": True})
    draw0 = api.debug("water_av_record_draw", {}).get("radius_m", 48.0)
    skin0 = api.debug("water_av_skin", {})
    api.debug("water_av_record_draw", {"radius": 48.0})
    out = {"pose": POSES[a.pose], "draw_m": 48.0, "skin_before": skin0}
    try:
        out["record"] = lay_lake(api)
        camera_set(api, POSES[a.pose]); time.sleep(5.0)
        for mode in ("off", "on"):
            api.debug("water_av_skin", {"on": mode == "on"})
            time.sleep(4.0)
            png = EV / f"{a.tag}_{mode}.png"
            grab(ff, a.window, a.crop, png)
            out[mode] = {"skin": api.debug("water_av_skin", {}), "rest": window(api, a.rest_s, f"skin {mode}, at rest")}
            print(mode, json.dumps(out[mode]["skin"]), flush=True)
            r = out[mode]["rest"]
            print(f"  rest: {r['fps_mean']} fps, cpu frame {r['cpu_frame_ms_mean']} ms, gpu median {r['gpu_frame_ms_median']} ms, "
                  f"worst frame {r['frame_ms_max']} ms, water app cpu {r['app_water_cpu_ms']}", flush=True)
        try:
            from PIL import Image, ImageChops
            off = Image.open(EV / f"{a.tag}_off.png").convert("RGB"); on = Image.open(EV / f"{a.tag}_on.png").convert("RGB")
            d = ImageChops.difference(off, on).convert("L")
            hist = d.histogram(); total = sum(hist)
            changed = sum(hist[16:])
            out["pixels_changed_over_16"] = round(changed / total, 4)
            d.point(lambda v: 255 if v >= 16 else 0).save(EV / f"{a.tag}_diff.png")
            print(f"pixels changed by the skin (grey >= 16): {100.0 * changed / total:.1f} %", flush=True)
        except Exception as e:
            print("diff:", e)
        for i, mode in enumerate(("on", "off", "on") if a.splash_ab else ("on",)):
            api.debug("water_av_skin", {"on": mode == "on"})
            time.sleep(3.0)
            key = "splash_on" if i == 0 else f"splash_{i}_{mode}"
            out["splash" if i == 0 else f"splash_{i}"] = splash(api)
            out[key] = window(api, 8.0, f"skin {mode}, 0-8 s after the splash")
            r = out[key]
            print(f"splash {i}, skin {mode}: {r['fps_mean']} fps mean, cpu frame {r['cpu_frame_ms_mean']} ms, worst frame {r['frame_ms_max']} ms, "
                  f"skin {json.dumps(api.debug('water_av_skin', {}))}", flush=True)
            time.sleep(30.0)
    finally:
        destroy_all(api)
        api.debug("water_av_skin", {"on": bool(skin0.get("on", False))})
        api.debug("water_av_record_draw", {"radius": draw0})
        (EV / f"{a.tag}.json").write_text(json.dumps(out, indent=1))
        print("wrote", EV / f"{a.tag}.json")


if __name__ == "__main__":
    main()
