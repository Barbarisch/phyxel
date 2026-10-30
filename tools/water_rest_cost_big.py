"""WaterCore A12 (45.1 item 6) - the rest motion's worst case: a large pond filling the screen at a low angle (fragment-bound).

WaterBench_Creek (port 8114), Release. The 60 x 28 m lake of 27.24 (x 4-63, z 2-29, 2 m deep - water_l2_fade_profile's `lake` rig),
the camera at its west end 3 m over the water looking east along it (the water fills most of the frame, out to the horizon - where the
per-pixel fade does its work). One variable: rest_amp_mm 0 / 3 (the largest of the owner's clips), ABAB, 8 s each, whole frame.
Prediction (written 2026-10-29): GPU +0.1-0.3 ms (4 restHeight calls a fragment, each 3 sines + a 3-D value noise of 8 hashes).

    python tools/water_rest_cost_big.py [--amp-mm 3] [--tag rest_cost_big]
Writes docs/evidence/water_core_m4/<tag>.json.
"""
import argparse, json, pathlib, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set, pose_ok
import water_l2_fade_profile as fp
from water_rest_look import frame_window

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_m4"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8114")
    ap.add_argument("--amp-mm", type=float, default=3.0)
    ap.add_argument("--tag", default="rest_cost_big")
    a = ap.parse_args()
    api = Api(a.url); st = api.wait_status(300)
    try: api.debug("editor_panels", {"item_equipper": False, "tool_panels": False})
    except Exception as e: print("editor_panels:", e)
    rest0 = (api.debug("water_render_core", {}).get("rest") or {})
    if not rest0: raise SystemExit("this build has no rest motion")
    out = {"build": st.get("build_config"), "amp_mm": a.amp_mm, "rows": []}
    try:
        api.debug("water_av_realtime", {"on": True})
        fp.make(api, fp.RIGS["lake"]["box"])
        pose = {"x": 2.0, "y": fp.TOP + 2 + 3.0, "z": 15.5, "yaw": 0.0, "pitch": -12.0}
        got = camera_set(api, pose)
        if not pose_ok(pose, got): raise SystemExit(f"camera pose not taken: {got}")
        out["pose"] = got
        time.sleep(2.0)
        for cond in ("off", "on", "off", "on", "off", "on"):
            api.debug("water_render_core", {"rest_amp_mm": 0.0 if cond == "off" else a.amp_mm})
            time.sleep(1.0)
            r = frame_window(api, 8.0); r["cond"] = cond
            out["rows"].append(r); print(r, flush=True)
    finally:
        (EV / f"{a.tag}.json").write_text(json.dumps(out, indent=1), encoding="utf-8")
        api.debug("water_render_core", {"rest_amp_mm": rest0.get("amp_mm", 0.0)})
        api.debug("clear_dynamics"); fp.destroy_all(api)


if __name__ == "__main__":
    main()
