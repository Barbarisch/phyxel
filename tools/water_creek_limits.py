"""The creek limits ladder (owner, 2026-10-23: "do the limits pass on ponds and creeks").

Where does one creek of pond water stop working - over the 2 ms budget, out of its channel, never steady, wrong books, or refused
by the volume cap? Measured on WaterBench_Creek (port 8114, Release, the GPU otherwise idle), one creek at a time, from dry:
a spring at the head (pumps across the head's 1/9 m cells), an outfall at the foot, Manning n 0.035 at the bed, the engine's
default bricks / sweeps / partial wake, auto-sleep off, the camera at the bench pose beside the head (2.5 m past the far bank) (the surface pass is costed by
what is seen - 26.30 - so the pose is fixed per creek and stated).

Three carved creeks (all 1/9 m-grid gravel beds, the head 1/3 m below the grass):
  main   x 10-109 (100 m) z 31-32 (2 m wide), 1:200 - the bench creek (water_creek_bench.py --build; never re-carved here)
  steep  x 10-69  (60 m)  z 40-41 (2 m wide), 1:20  - the fast current           (--carve builds it)
  wide   x 10-69  (60 m)  z 50-55 (6 m wide), 1:200 - the broad shallow creek     (--carve builds it)
Ladders:
  --flow   q in --qs on each creek in --creeks (the full length)
  --length main creek at q --len-q, the volume over the first L m of it (outfall at L), L in --lengths
Per run: polled every second until steady (the outflow over the last 10 s within 3 % of q) or --max-s; then a --window s steady
window: the solver's GPU ms (median / max), substeps, bricks, the GPU's WaterSurface pass and frame, fps, the mid-reach depth and
surface speed (water_av_column), the bank columns' water (any = OVERTOP: the channel is full - a bench geometry limit, not the
engine's), and the books (stored - (in - out)).
Manning's prediction is written beside each: depth (q n / w sqrt S)^0.6, speed q / (w d).

    python tools/water_creek_limits.py --carve            # once: carve steep + wide beside the bench creek, save
    python tools/water_creek_limits.py --flow --length    # the ladders -> docs/evidence/water_core_h1/creek_limits.json
"""
import argparse, json, math, pathlib, statistics, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set, world_job

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_h1"
H, N, GRID, LINE0 = 1.0 / 9.0, 0.035, 9, 15.67
CREEKS = {
    "main":  {"x0": 10, "x1": 109, "z0": 31, "z1": 32, "slope": 1 / 200},
    "steep": {"x0": 10, "x1": 69,  "z0": 40, "z1": 41, "slope": 1 / 20},
    "wide":  {"x0": 10, "x1": 69,  "z0": 50, "z1": 55, "slope": 1 / 200},
}


def bed_top(c, x):
    """the carve's top at voxel x (m): the bed line at the voxel's centre floored to the 1/9 m grid"""
    return math.floor((LINE0 - c["slope"] * (x + 0.5 - c["x0"])) * GRID + 1e-4) / GRID


def carve(api, name):
    c = CREEKS[name]
    subs, micros = [], []
    for x in range(c["x0"], c["x1"] + 1):
        b = bed_top(c, x)
        bv = math.floor(b + 1e-6)                                    # the voxel the bed's top lies in
        world_job(api, "/api/world/clear", {"x1": x, "y1": bv, "z1": c["z0"], "x2": x, "y2": 15, "z2": c["z1"]})
        nine = int(round((b - bv) * 9))
        layers, rem = nine // 3, nine % 3
        for z in range(c["z0"], c["z1"] + 1):
            for sx in range(3):
                for sz in range(3):
                    for sy in range(layers):
                        subs.append({"x": x, "y": bv, "z": z, "sx": sx, "sy": sy, "sz": sz, "material": "Gravel"})
                    for my in range(rem):
                        for mx in range(3):
                            for mz in range(3):
                                micros.append({"x": x, "y": bv, "z": z, "sx": sx, "sy": layers, "sz": sz, "mx": mx, "my": my, "mz": mz, "material": "Gravel"})
    for i in range(0, len(subs), 500): api.post("/api/world/subcubes/batch", {"subcubes": subs[i:i + 500]})
    for i in range(0, len(micros), 500): api.post("/api/world/microcubes/batch", {"microcubes": micros[i:i + 500]})
    print(f"carved {name}: {len(subs)} subcubes + {len(micros)} microcubes")


def check_carve(api, name):
    """carved = the creek's top voxel at three x is no longer a whole Grass cube"""
    c = CREEKS[name]
    bad = []
    for x in (c["x0"], (c["x0"] + c["x1"]) // 2, c["x1"]):
        v = api.get(f"/api/world/voxel?x={x}&y=15&z={c['z0']}")
        if v.get("material") == "Grass": bad.append(f"{name} ({x},15,{c['z0']}) is still Grass")
    return bad


def manning(c, q):
    w = c["z1"] - c["z0"] + 1
    d = (q / w * N / math.sqrt(c["slope"])) ** 0.6
    return d, q / (w * d)


def med(xs):
    xs = [x for x in xs if x is not None]
    return statistics.median(xs) if xs else None


def passes(api, frames=240):
    d = api.get(f"/api/debug/gpu_timing?frames={frames}")
    out = {"frame_gpu_ms": (d.get("gpu_frame_ms") or {}).get("median_ms")}
    for sc in d.get("scopes", []):
        if sc.get("name") == "WaterSurface": out["surface_ms"] = round(sc.get("median_ms", 0.0), 4)
    return out


def destroy_all(api):
    for v in api.debug("water_av_list").get("volumes", []): api.debug("water_av_destroy", {"id": v["id"], "discard": True})


def run(api, name, q, length=None, max_s=480.0, window=15.0):
    c = dict(CREEKS[name])
    if length: c["x1"] = c["x0"] + length - 1
    w = c["z1"] - c["z0"] + 1
    ylo = math.floor(bed_top(c, c["x1"])) - 1
    box = {"x1": c["x0"], "y1": ylo, "z1": c["z0"] - 2, "x2": c["x1"], "y2": 17, "z2": c["z1"] + 2}
    cells = 729 * (box["x2"] - box["x1"] + 1) * (box["y2"] - box["y1"] + 1) * (box["z2"] - box["z1"] + 1)
    dn, vn = manning(c, q)
    out = {"creek": name, "q": q, "length_m": c["x1"] - c["x0"] + 1, "width_m": w, "slope": c["slope"], "box": box, "box_cells": cells,
           "manning_depth_m": dn, "manning_speed_ms": vn}
    destroy_all(api)
    r = api.debug("water_av_create", {**box, "cellSize": H, "backend": "gpu", "auto_sleep": False, "manning_n": N})
    if "error" in r:
        out["refused"] = r["error"]; print(f"  {name} q {q} L {out['length_m']}: REFUSED - {r['error']}", flush=True); return out
    vid = r["volume"]["id"]
    zs = [c["z0"] + (k + 0.5) * H for k in range(w * 9)]
    # a GPU volume holds at most 64 sources (WaterCoreGpu kMaxSources): an outfall on every foot cell, the springs spread over the
    # head with what is left (a 6 m creek: 54 outfalls + 10 springs); every reply is checked (2026-10-23: unchecked, the wide
    # creek's run lost 44 of its 54 springs and got 19 % of its flow)
    def src(body):
        rr = api.debug("water_av_source", {"id": vid, **body})
        if "error" in rr: raise SystemExit(f"water_av_source refused: {rr['error']}")
    for z in zs: src({"x": c["x1"] + 1 - H / 2, "y": bed_top(c, c["x1"]) + H / 2, "z": z, "outfall": True})
    ns = min(len(zs), 64 - len(zs))
    springs = [zs[int((k + 0.5) * len(zs) / ns)] for k in range(ns)]
    out["springs"], out["outfalls"] = ns, len(zs)
    camera_set(api, {"x": c["x0"] + 2.0, "y": 18.2, "z": c["z1"] + 2.5, "yaw": -12.0, "pitch": -16.0})
    time.sleep(1.0)
    for z in springs: src({"x": c["x0"] + H / 2, "y": bed_top(c, c["x0"]) + H / 2, "z": z, "rate": q / ns})
    xm = (c["x0"] + c["x1"]) // 2
    mid = [(x + 0.5, z + 0.5) for x in range(xm - 5, xm + 6, 2) for z in range(c["z0"], c["z1"] + 1)]
    banks = [(x + 0.5, z) for x in range(c["x0"] + 1, c["x1"], max(1, (c["x1"] - c["x0"]) // 12)) for z in (c["z0"] - 1.5, c["z1"] + 2.5)]
    polls, t0, steady_at = [], time.time(), None

    def sample():
        v = next(x for x in api.debug("water_av_list")["volumes"] if x["id"] == vid)
        cols = api.debug("water_av_column", {"points": [list(p) for p in mid + banks]}).get("columns", [])
        mc, bc = cols[:len(mid)], cols[len(mid):]
        wet = [k for k in mc if k.get("surface_y") is not None]
        bw = [k for k in bc if k.get("surface_y") is not None and k["surface_y"] - 16.0 > 0.01]
        return {"t": round(time.time() - t0, 2), "in_m3": v.get("source_in_m3", 0.0), "out_m3": v.get("outfall_out_m3", 0.0), "stored_m3": v["mass"],
                "solver_ms": v.get("gpu_device_ms"), "substeps": v["last"]["substeps"], "bricks": v.get("gpu_bricks"), "asleep": v.get("asleep"),
                "mid_depth_m": (sum(k["surface_y"] - bed_top(c, math.floor(k["x"])) for k in wet) / len(wet)) if wet else 0.0,
                "mid_surface_speed_ms": (sum(abs(k.get("u", 0.0)) for k in wet) / len(wet)) if wet else 0.0,
                "bank_wet": len(bw), "bank_depth_max_m": max((k["surface_y"] - 16.0 for k in bw), default=0.0)}

    while time.time() - t0 < max_s:
        polls.append(sample())
        past = [p for p in polls if p["t"] <= polls[-1]["t"] - 10.0]
        if past and polls[-1]["out_m3"] > 1e-3:
            rate = (polls[-1]["out_m3"] - past[-1]["out_m3"]) / (polls[-1]["t"] - past[-1]["t"])
            if abs(rate / q - 1.0) < 0.03: steady_at = polls[-1]["t"]; break
        time.sleep(1.0)
    out["steady_s"] = steady_at
    out["first_outflow_s"] = next((p["t"] for p in polls if p["out_m3"] > 1e-4), None)
    tw = time.time()
    win = []
    while time.time() - tw < window:
        win.append(sample()); time.sleep(0.5)
    out.update(passes(api))
    et = api.get("/api/debug/engine_timing")
    a, b = win[0], win[-1]
    out.update({"out_over_in_window": (b["out_m3"] - a["out_m3"]) / max(1e-6, b["t"] - a["t"]) / q,
                "solver_ms_median": med([p["solver_ms"] for p in win]), "solver_ms_max": max((p["solver_ms"] or 0) for p in win),
                "substeps_max": max(p["substeps"] for p in win), "bricks": med([p["bricks"] for p in win]), "fps": et.get("fps"),
                "mid_depth_m": med([p["mid_depth_m"] for p in win]), "mid_surface_speed_ms": med([p["mid_surface_speed_ms"] for p in win]),
                "bank_wet_max": max(p["bank_wet"] for p in win), "bank_depth_max_m": max(p["bank_depth_max_m"] for p in win),
                "books_error_m3": b["stored_m3"] - (b["in_m3"] - b["out_m3"]), "stored_m3": b["stored_m3"]})
    out["water_ms"] = (out.get("solver_ms_median") or 0.0) + (out.get("surface_ms") or 0.0)
    out["polls"] = polls + win
    destroy_all(api)
    print(f"  {name} q {q} L {out['length_m']}: steady {steady_at} s, solver {out['solver_ms_median']:.2f}/{out['solver_ms_max']:.2f} ms, surface "
          f"{out.get('surface_ms', 0):.2f}, water {out['water_ms']:.2f} ms, substeps {out['substeps_max']}, bricks {out['bricks']}, depth {out['mid_depth_m']:.3f} "
          f"(Manning {dn:.3f}), speed {out['mid_surface_speed_ms']:.2f} (Manning mean {vn:.2f}), out/in {out['out_over_in_window']:.3f}, banks wet {out['bank_wet_max']}, "
          f"books {out['books_error_m3']:.2e} m3, fps {out['fps']}", flush=True)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8114")
    ap.add_argument("--carve", action="store_true")
    ap.add_argument("--flow", action="store_true")
    ap.add_argument("--length", action="store_true")
    ap.add_argument("--creeks", default="main,steep,wide")
    ap.add_argument("--qs", default="0.05,0.1,0.2,0.4,0.8,1.6")
    ap.add_argument("--lengths", default="25,50,100")
    ap.add_argument("--len-q", type=float, default=0.2)
    ap.add_argument("--max-s", type=float, default=480.0)
    ap.add_argument("--out", default="creek_limits.json")
    a = ap.parse_args()
    api = Api(a.url); api.wait_status(300)
    if a.carve:
        for name in ("steep", "wide"): carve(api, name)
        print("save:", str(api.post("/api/world/save", {}))[:120])
    bad = sum((check_carve(api, n) for n in CREEKS), [])
    if bad: raise SystemExit("not carved (run --carve): " + "; ".join(bad))
    if not (a.flow or a.length): return
    api.debug("water_av_realtime", {"on": True})
    destroy_all(api)
    camera_set(api, {"x": 12.0, "y": 18.2, "z": 34.5, "yaw": -12.0, "pitch": -16.0}); time.sleep(2.0)
    res = {"control": {**passes(api), "fps": api.get("/api/debug/engine_timing").get("fps")}, "runs": [], "args": vars(a)}
    print("control:", res["control"], flush=True)
    path = EV / a.out
    def save(): EV.mkdir(parents=True, exist_ok=True); path.write_text(json.dumps(res, indent=1))
    if a.length:
        for L in [int(x) for x in a.lengths.split(",")]:
            res["runs"].append({"ladder": "length", **run(api, "main", a.len_q, L, a.max_s)}); save()
    if a.flow:
        for name in a.creeks.split(","):
            for q in [float(x) for x in a.qs.split(",")]:
                res["runs"].append({"ladder": "flow", **run(api, name, q, None, a.max_s)}); save()
    print("wrote", path)


if __name__ == "__main__":
    main()
