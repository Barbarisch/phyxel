"""58.5 item 1: the river reach's black/white noise, live. RiverCarve (8113): the W5.2b flight's first pose; wait for the reach
under the camera; then (a) the reach's host field roughness (grid-scale RMS, the same metric as the rig) in a 6 m window inside the
reach; (b) window captures: shipped / the river ripple layer off / the tonemap curve off (debug)."""
import json, math, shutil, subprocess, sys, time
sys.path.insert(0, "tools")
from water_bench import Api, camera_set
import water_w52b_follow as F
import water_w5_river_current as w51

OUT = sys.argv[1]
api = Api("http://127.0.0.1:8113"); api.wait_status(600)
ff = shutil.which("ffmpeg")
camera_set(api, w51.SITES["river"]["cam"]); time.sleep(12.0)
path = F.trace(api)
wps = F.waypoints(path)
camera_set(api, wps[0]); time.sleep(15.0)


def grab(name):
    subprocess.run([ff, "-y", "-loglevel", "error", "-f", "gdigrab", "-draw_mouse", "0", "-i", "title=WaterBench_RiverCarve",
                    "-frames:v", "1", f"{OUT}/{name}.png"], check=True)


reach = None
for a in api.debug("water_av_list", {}).get("volumes", []):
    print("volume", {k: a.get(k) for k in ("id", "is_reach", "backend", "boundary_columns", "asleep", "awake_columns")}, "box", a.get("minVoxel"), a.get("maxVoxel"))
    if a.get("is_reach"): reach = a
print("camera", wps[0])
if reach:
    rid = reach["id"]
    # a 6 m window at the river point under the first waypoint's look target
    px, pz = path[3][0], path[3][1]
    d = api.debug("water_surface_dump", {"x1": px - 3, "z1": pz - 3, "x2": px + 3, "z2": pz + 3})
    for f in d.get("fields", []):
        nx, nz = f["nx"], f["nz"]; cols = f["cols"]
        def top(x, z):
            c = cols[z * nx + x]
            return c[0][-1][1] if c and c[0] else float("nan")
        c2 = s2 = 0.0; n = 0; mx = 0.0
        for z in range(1, nz - 1):
            for x in range(1, nx - 1):
                t, a_, b, c0, c1 = top(x, z), top(x - 1, z), top(x + 1, z), top(x, z - 1), top(x, z + 1)
                if any(math.isnan(v) for v in (t, a_, b, c0, c1)): continue
                ck = t - 0.25 * (a_ + b + c0 + c1)
                c2 += ck * ck; n += 1; mx = max(mx, abs(ck))
                s2 += ((b - a_) / (2 * f["h"])) ** 2 + ((c1 - c0) / (2 * f["h"])) ** 2
        print(f"field src {f['source']} h {f['h']:.3f} at ({px:.0f},{pz:.0f}): n {n} grid-scale RMS {1000 * math.sqrt(c2 / n) if n else -1:.3f} mm max {1000 * mx:.2f} mm slope RMS {math.sqrt(s2 / n) if n else -1:.4f}")
grab("reach_shipped")
print("ripples off", api.debug("water_ripples", {"river": {"on": False}}).get("river"))
time.sleep(2.0); grab("reach_ripples_off")
api.debug("water_ripples", {"river": {"on": True}})
time.sleep(2.0); grab("reach_ripples_on_again")
