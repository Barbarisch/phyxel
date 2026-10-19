"""The owner's "water looks a little spiky while it settles on top of the dropped stone" (2026-10-18): measure WHERE the spikes come
from before changing anything. The same rig as water_splash_variants_video.py (46 x 28 m pond, 1 m deep, sand bed, a 1 m stone near
the south-west corner, droplets at the default), recorded from the window while the surface field round the stone is polled
(water_surface_dump) with the ripple layer's largest height (water_ripples).

Per sample, over the dump rect:
  field_rise  - the highest run top above the pond's level (median top): a spike already in the FIELD (the solver / extraction)
  mesh_rise   - the highest top-quad corner above the pond's level, the corners rebuilt exactly as buildWaterSurfaceMeshRect does
  quad_span   - the largest corner-to-corner height inside one 11 cm top quad (a tall thin triangle)
  ripple_max  - the ripple layer's largest |height| (added in the vertex shader, not in the field)
  carry_l     - the over-full carry since the volume was made (litres): made in a body's cells without a displacement rate / made elsewhere / parked at a ceiling / made in a body's cells WITH a rate

    python tools/water_stone_spikes.py [--seconds 7] [--tag spikes]
Writes docs/evidence/water_core_h1/<tag>.mp4 and <tag>.json.
"""
import argparse, json, pathlib, shutil, statistics, subprocess, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set, world_job

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_h1"


def mesh_corners(f):
    """The top-quad corners of every run, as buildWaterSurfaceMeshRect builds them (cornerY: the mean of the tops of the up to four
    sub-columns round the corner holding a run that overlaps [bottom, top + h); the column itself always counts). Columns outside the
    dump count as absent (the dump's ring is not drawn here). Yields (x, z, run, own top, [y00, y10, y11, y01], body)."""
    nx, nz, h = f["nx"], f["nz"], f["h"]
    cols = f["cols"]
    at = lambda x, z: cols[x + nx * z] if 0 <= x < nx and 0 <= z < nz else None
    for z in range(nz):
        for x in range(nx):
            c = at(x, z)
            for r, (b, t) in enumerate(c[0]):
                lo, hi = b, t + h
                ys = []
                for cx, cz in ((0, 0), (1, 0), (1, 1), (0, 1)):
                    s, n = 0.0, 0
                    for dz in (-1, 0):
                        for dx in (-1, 0):
                            xx, zz = x + cx + dx, z + cz + dz
                            if xx == x and zz == z: s += t; n += 1; continue
                            o = at(xx, zz)
                            if o is None: continue
                            run = next((rr for rr in o[0] if rr[1] > lo and rr[0] < hi), None)
                            if run: s += run[1]; n += 1
                    ys.append(s / n)
                yield x, z, r, t, ys, c[2]


def analyse(f):
    tops = [c[0][-1][1] for c in f["cols"] if c[0]]
    if not tops: return None
    level = statistics.median(tops)
    field_rise = max(tops) - level
    mesh_rise, quad_span, worst = 0.0, 0.0, None
    runs_max = max(len(c[0]) for c in f["cols"])
    for x, z, r, t, ys, body in mesh_corners(f):
        span = max(ys) - min(ys)
        if span > quad_span: quad_span, worst = span, {"x": f["x0"] + x, "z": f["z0"] + z, "run": r, "top": t, "corners": ys, "body": body}
        mesh_rise = max(mesh_rise, max(ys) - level)
    return {"level": level, "field_rise": field_rise, "mesh_rise": mesh_rise, "quad_span": quad_span, "runs_max": runs_max, "worst_quad": worst}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8114")
    ap.add_argument("--window", default="WaterBench_Creek")
    ap.add_argument("--seconds", type=float, default=7.0)
    ap.add_argument("--pose", default="-1,21.5,32,-49,-22")
    ap.add_argument("--crop", default="954:560:243:60")
    ap.add_argument("--ripples", choices=["keep", "off"], default="keep", help="off = the A/B: the ripple layer disabled")
    ap.add_argument("--no-video", action="store_true")
    ap.add_argument("--size", default="46x28", help="the pond (m); a taller lid needs a smaller pond (the 2,000,000-cell cap)")
    ap.add_argument("--lid", type=int, default=17, help="the volume's top voxel row (17 = the rig's 2 m box: 1 m of air over the pond)")
    ap.add_argument("--probe", action="store_true", help="read the cells of the tallest column the same frame (a GPU read-back each poll)")
    ap.add_argument("--tag", default="spikes")
    args = ap.parse_args()
    ff = shutil.which("ffmpeg")
    api = Api(args.url); api.wait_status(300)
    try: api.debug("editor_panels", {"item_equipper": False, "tool_panels": False})
    except Exception as e: print("editor_panels:", e)
    l, w = (int(v) for v in args.size.split("x")); x0, z0 = 2, 0
    world_job(api, "/api/world/clear", {"x1": x0, "y1": 15, "z1": z0, "x2": x0 + l - 1, "y2": 15, "z2": z0 + w - 1})
    world_job(api, "/api/world/fill", {"x1": x0, "y1": 15, "z1": z0, "x2": x0 + l - 1, "y2": 15, "z2": z0 + w - 1}, material="Sand")
    for b in ((x0 - 1, z0 - 1, x0 - 1, z0 + w), (x0 + l, z0 - 1, x0 + l, z0 + w), (x0 - 1, z0 - 1, x0 + l, z0 - 1), (x0 - 1, z0 + w, x0 + l, z0 + w)):
        world_job(api, "/api/world/fill", {"x1": b[0], "y1": 16, "z1": b[1], "x2": b[2], "y2": 17, "z2": b[3]}, material="StoneBricks")
    api.debug("water_av_realtime", {"on": True})
    api.debug("water_coupling", {"enabled": True, "solids": True})
    if args.ripples == "off": api.debug("water_ripples", {"enabled": False})
    for v in api.debug("water_av_list").get("volumes", []): api.debug("water_av_destroy", {"id": v["id"]})
    box = {"x1": x0, "y1": 16, "z1": z0, "x2": x0 + l - 1, "y2": args.lid, "z2": z0 + w - 1}
    cr = api.debug("water_av_create", {**box, "cellSize": 1 / 9, "backend": "gpu", "auto_sleep": False, "partial_wake": True})
    if "error" in cr: sys.exit(f"water_av_create: {cr['error']}")
    vid = cr["volume"]["id"]
    api.debug("place_water_box", {"target": "core", **{**box, "y2": 16}, "mass": 1.0})
    p = [float(v) for v in args.pose.split(",")]
    camera_set(api, {"x": p[0], "y": p[1], "z": p[2], "yaw": p[3], "pitch": p[4]})
    time.sleep(4.0)
    sx, sz = x0 + 4.0, z0 + w - 4.0
    rect = {"x1": sx - 1.5, "z1": sz - 1.5, "x2": sx + 1.5, "z2": sz + 1.5}
    mp4 = EV / f"{args.tag}.mp4"
    rec = None
    if not args.no_video:
        rec = subprocess.Popen([ff, "-y", "-loglevel", "error", "-f", "gdigrab", "-framerate", "30", "-t", str(args.seconds),
                                "-i", f"title={args.window}", "-c:v", "libx264", "-pix_fmt", "yuv420p", str(mp4)])
    t0 = time.time()
    time.sleep(1.0)
    api.debug("spawn_gpu_particle", {"x": sx, "y": 20.0, "z": sz, "material": "Stone", "scale": 1.0, "lifetime": args.seconds})
    t_stone = time.time() - t0
    rows, dumps = [], []
    while time.time() - t0 < args.seconds:
        d = api.debug("water_surface_dump", {**rect, "cells": args.probe})
        rp = api.debug("water_ripples", {})
        vrec = next((v for v in api.debug("water_av_list").get("volumes", []) if v["id"] == vid), {})
        t = round(time.time() - t0, 2)
        carry = [vrec.get(k, 0.0) * 1000.0 for k in ("carry_body_m3", "carry_flow_m3", "carry_parked_m3", "carry_rate_m3")]   # litres since creation
        for f in d.get("fields", []):
            a = analyse(f)
            if a:
                a.update({"t": t, "ripple_max": rp.get("max_abs_m"), "ripple_slope": rp.get("max_slope"), "carry_l": carry})
                c = f.get("cells")
                if args.probe and c and a["field_rise"] > 0.3:   # the tallest column's cells, read the same frame: water f and body s
                    k0 = max(0, int(round((a["level"] - 0.45 - c["y0"]) / c["h"])))
                    print(f"   cells x {c['x']:.2f} z {c['z']:.2f} top {c['top']:.3f}: " + " ".join(
                        f"{c['y0'] + (k + 0.5) * c['h']:.2f}:f{v[0]:.3f}/s{v[1]:.2f}" for k, v in enumerate(c["fs"]) if k >= k0 and (v[0] > 1e-4 or v[1] > 1e-4)), flush=True)
                rows.append(a); dumps.append({"t": t, "field": f})
                print(f"t {t:5.2f}  level {a['level']:.3f}  field_rise {a['field_rise']:.3f}  mesh_rise {a['mesh_rise']:.3f}  "
                      f"quad_span {a['quad_span']:.3f}  runs {a['runs_max']}  ripple {(a['ripple_max'] or 0):.3f}  carry L body/flow/parked/rate "
                      f"{carry[0]:.1f} {carry[1]:.1f} {carry[2]:.1f} {carry[3]:.1f}", flush=True)
        time.sleep(0.08)
    if rec: rec.wait()
    if args.ripples == "off": api.debug("water_ripples", {"enabled": True})
    (EV / f"{args.tag}.json").write_text(json.dumps({"stone_s": t_stone, "rect": rect, "rows": rows, "dumps": dumps}))
    print("wrote", EV / f"{args.tag}.json")


if __name__ == "__main__":
    main()
