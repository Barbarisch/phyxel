"""WaterCore §58.5: the live river reach's grid-scale roughness, split - (a) by distance to a bed step (a sub-column whose solid top
differs from a neighbour's within 0.5 m), (b) the checkerboard share: the part of each column's deviation from its 4 neighbours'
mean that alternates sign with (-1)^(x+z) (a numerical odd-even mode), and (c) by water depth. RiverCarve (port 8113) at the W5.2b
flight's first pose, the river ripple layer off (the sim's own heights), 15 s after the reach is made; 4 windows of 6 m along the river.
    python tools/water_reach_noise_split.py
"""
import math, sys, time, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set
import water_w52b_follow as F
import water_w5_river_current as w51

api = Api("http://127.0.0.1:8113"); api.wait_status(600)
api.debug("water_ripples", {"river": {"on": False}})
camera_set(api, w51.SITES["river"]["cam"]); time.sleep(12.0)
path = F.trace(api)
wps = F.waypoints(path)
camera_set(api, wps[0]); time.sleep(15.0)
rid = next((a["id"] for a in api.debug("water_av_list", {}).get("volumes", []) if a.get("is_reach")), None)
print("reach", rid)
near = {"n": 0, "c2": 0.0}; far = {"n": 0, "c2": 0.0}
alt = 0.0; tot = 0.0
bydepth = {}
for k in (2, 3, 4, 5):
    px, pz = path[k][0], path[k][1]
    d = api.debug("water_surface_dump", {"x1": px - 3, "z1": pz - 3, "x2": px + 3, "z2": pz + 3})
    for f in d.get("fields", []):
        if f.get("source") != rid: continue
        nx, nz, cols, x0, z0 = f["nx"], f["nz"], f["cols"], f["x0"], f["z0"]

        def top(x, z):
            c = cols[z * nx + x]
            return c[0][-1][1] if c and c[0] else float("nan")

        def solid(x, z):
            c = cols[z * nx + x]
            return c[1] if c and c[1] is not None else float("nan")
        for z in range(1, nz - 1):
            for x in range(1, nx - 1):
                t, a, b, c0, c1 = top(x, z), top(x - 1, z), top(x + 1, z), top(x, z - 1), top(x, z + 1)
                if any(math.isnan(v) for v in (t, a, b, c0, c1)): continue
                ck = t - 0.25 * (a + b + c0 + c1)
                sgn = 1 if ((x0 + x) + (z0 + z)) % 2 == 0 else -1
                alt += sgn * ck; tot += abs(ck)
                s = solid(x, z)
                step = False
                for dz in range(-4, 5):
                    for dx in range(-4, 5):
                        xx, zz = x + dx, z + dz
                        if 0 <= xx < nx and 0 <= zz < nz:
                            o = solid(xx, zz)
                            if not math.isnan(o) and not math.isnan(s) and abs(o - s) > 0.5: step = True
                bucket = near if step else far
                bucket["n"] += 1; bucket["c2"] += ck * ck
                depth = t - s if not math.isnan(s) else float("nan")
                key = "nan" if math.isnan(depth) else f"{min(int(depth * 2) / 2, 3.0):.1f}"
                e = bydepth.setdefault(key, [0, 0.0]); e[0] += 1; e[1] += ck * ck
rms = lambda b: 1000 * math.sqrt(b["c2"] / b["n"]) if b["n"] else -1
print(f"within 0.5 m of a bed step: {near['n']} sub-columns, grid-scale RMS {rms(near):.3f} mm")
print(f"away from steps:            {far['n']} sub-columns, grid-scale RMS {rms(far):.3f} mm")
print(f"checkerboard share |sum (-1)^(x+z) dev| / sum |dev| = {abs(alt) / tot if tot else 0:.3f} (a random field ~0; an odd-even mode -> 1)")
for k in sorted(bydepth):
    n, c2 = bydepth[k]
    print(f"  depth {k} m: {n} sub-columns, RMS {1000 * math.sqrt(c2 / n):.3f} mm")
