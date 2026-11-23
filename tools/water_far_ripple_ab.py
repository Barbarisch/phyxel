"""WaterCore §58.8 (item 6) - the far ripple A/B, live (WaterBench_Creek, port 8114; the 60 m record lake). A splash at the lake's
centre; the camera 8 m from it, 3 m up, looking across (inside the ripple reach); at 3 s after the splash a still of the same frame with the far ripple OFF and ON (water_ripples
{far: {on}}), and the slope view (water_render_core debug 9, tonemap curve 0: the ripple slope as grey) each way. Writes
docs/evidence/water_core_m4/far_ripple_{off,on}{,_slope}.png and far_ripple_sheet.png.
    python tools/water_far_ripple_ab.py
"""
import pathlib, shutil, subprocess, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set
import water_play_scene as P

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_m4"
# within the ripple reach (a layer exists only within ~21 m of the viewer - 50 m back, layers 0: nothing to derive from); 8 m from the
# splash, looking across it - the distance fade (13 -> 21 m) in the middle of the frame, the far ripple past it
CAM = {"x": P.X0 + P.W / 2, "y": P.TOP + 3.0, "z": P.Z0 + P.W / 2 + 8.0, "yaw": -90.0, "pitch": -8.0}


def main():
    import argparse
    ap = argparse.ArgumentParser(); ap.add_argument("--gain", type=float, default=1.0); ap.add_argument("--tag", default="far_ripple"); a = ap.parse_args()
    ff = shutil.which("ffmpeg")
    api = Api("http://127.0.0.1:8114"); api.wait_status(600)
    try: api.debug("editor_panels", {"item_equipper": False, "tool_panels": False})
    except Exception as e: print("editor_panels:", e)
    api.debug("water_av_realtime", {"on": True})
    camera_set(api, CAM); time.sleep(10.0)

    def grab(name):
        subprocess.run([ff, "-y", "-loglevel", "error", "-f", "gdigrab", "-draw_mouse", "0", "-i", "title=WaterBench_Creek", "-frames:v", "1",
                        "-vf", "crop=954:560:243:60", str(EV / f"{name}.png")], check=True)

    for on in (False, True):
        print("far", api.debug("water_ripples", {"far": {"on": on, "gain": a.gain}}).get("far"), flush=True)
        api.debug("water_av_impulse", {"x": P.X0 + P.W / 2, "y": P.TOP + 1.9, "z": P.Z0 + P.W / 2, "radius": 2.0, "strength": 8.0, "dx": 0.0, "dy": -1.0, "dz": 0.0})
        time.sleep(3.0)
        tag = "on" if on else "off"
        grab(f"{a.tag}_{tag}")
        api.post("/api/debug/tonemap", {"curve": 0}); api.debug("water_render_core", {"debug": 9}); time.sleep(0.5)
        grab(f"{a.tag}_{tag}_slope")
        api.debug("water_render_core", {"debug": 0}); api.post("/api/debug/tonemap", {"curve": 1})
        time.sleep(25.0)   # the patch settles before the other take
    api.debug("water_ripples", {"far": {"on": False}})
    subprocess.run([ff, "-y", "-loglevel", "error"] + sum([["-i", str(EV / f"{a.tag}_{n}.png")] for n in ("off", "on", "off_slope", "on_slope")], []) +
                   ["-filter_complex", "[0][1][2][3]xstack=inputs=4:layout=0_0|w0_0|0_h0|w0_h0", str(EV / f"{a.tag}_sheet.png")], check=True)
    print("wrote", EV / f"{a.tag}_sheet.png")


if __name__ == "__main__":
    main()
