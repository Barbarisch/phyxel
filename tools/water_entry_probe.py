"""Measure a stone entering the Small bench pond (owner feedback 2026-10-10: "the stone sinks too fast, not
slowed by the water"; "the surface deforms a little slow - it looks thicker than water").

Drop 1 traces the stone through the water (water_coupling wet_body_list: centre + velocity of every body in a
water box, every poll) and compares it with a reference: a cube of the stone's size and density falling through
still water with quadratic drag, buoyancy and added mass,
    (rho_s + Ca rho_w) V dv/dt = (rho_s - rho_w) V g - 1/2 rho_w Cd A v|v|
(Cd 1.05 face-on cube, Ca 0.5 - both hedged: a tumbling cube sits between ~0.8 and 1.05 in Cd).
Drop 2 traces the drawn surface (water_av_probe surface_y) at 0, 1/3, 2/3, 1 and 4/3 m from the entry.

    python tools/water_entry_probe.py [--height 3.4] [--rho-stone 2700] [--out docs/evidence/water_core_e/entry_probe.json]
"""
import argparse
import json
import math
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from water_bench import Api, camera_set  # noqa: E402
from water_splash_demo import DROP_X, DROP_Z, POND_TOP, POSES, build_pond  # noqa: E402

REST = 16.5          # the pond's rest level (world y): water y 15..16.5 over a stone floor at 15
FLOOR = 15.0


def reference(v0, y0, edge, rho_s, cd=1.05, ca=0.5, g=9.81, rho_w=1000.0, dt=1e-3):
    """Still-water descent from the moment the cube's bottom touches the surface (y0 = its centre then)."""
    V, A = edge ** 3, edge ** 2
    y, v, t, out = y0, v0, 0.0, []
    while y - edge / 2 > FLOOR and t < 3.0:
        sub = min(1.0, max(0.0, (REST - (y - edge / 2)) / edge))   # submerged fraction
        m_eff = rho_s * V + ca * rho_w * V * sub
        f = -(rho_s - rho_w * sub) * V * g - 0.5 * rho_w * cd * A * sub * v * abs(v)   # drag opposes v (v < 0 going down: drag up)
        v += f / m_eff * dt
        y += v * dt
        t += dt
        out.append((t, y, v))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8111")
    ap.add_argument("--height", type=float, default=3.4, help="drop height above the pond (m)")
    ap.add_argument("--rho-stone", type=float, default=2700.0, help="reference stone density (kg/m^3; granite ~2700)")
    ap.add_argument("--out", default="docs/evidence/water_core_e/entry_probe.json")
    args = ap.parse_args()
    api = Api(args.url); api.wait_status(60)
    build_pond(api)
    api.debug("water_droplets", {"enabled": True})
    camera_set(api, POSES["mid"])
    time.sleep(5)
    edge = 1.0 / 3.0

    # ── drop 1: the stone ──
    api.debug("clear_dynamics"); time.sleep(0.3)
    api.debug("spawn_gpu_particle", {"x": DROP_X, "y": POND_TOP + args.height, "z": DROP_Z, "material": "Stone", "scale": edge, "lifetime": 30.0})
    trace, t0 = [], time.time()
    while time.time() - t0 < 3.0:
        bl = api.debug("water_coupling", {}).get("wet_body_list", [])
        if bl:
            b = min(bl, key=lambda b: abs(b["centre"][0] - DROP_X) + abs(b["centre"][2] - DROP_Z))
            trace.append((round(time.time() - t0, 4), b["centre"][1], b["velocity"][1]))
        time.sleep(0.005)
    # entry: the first sample whose bottom is at or below the rest level
    entry = next((i for i, s in enumerate(trace) if s[1] - edge / 2 <= REST), None)
    stone = {"samples": len(trace), "trace": trace}
    if entry is not None and entry > 0:
        # back-interpolate the touch moment from the previous sample (free fall above the surface)
        te, ye, ve = trace[entry]
        ref = reference(ve, ye, edge, args.rho_stone)
        floor_t = next((s[0] for s in trace[entry:] if s[1] - edge / 2 <= FLOOR + 0.02), None)
        ref_floor = ref[-1][0] if ref else None
        print(f"stone: entered at {ve:+.2f} m/s (sample t {te:.3f} s, centre y {ye:.2f})")
        print(f"  engine: reached the floor {floor_t - te:.3f} s after entry" if floor_t else "  engine: did not reach the floor in 3 s")
        print(f"  reference (Cd 1.05, Ca 0.5, rho {args.rho_stone:.0f}): {ref_floor:.3f} s, speed at the floor {ref[-1][2]:+.2f} m/s")
        for depth in (0.25, 0.5, 0.75, 1.0, 1.25):
            y = REST - depth + edge / 2   # centre when the bottom is `depth` under the rest level
            e = next((s for s in trace[entry:] if s[1] <= y), None)
            r = next((s for s in ref if s[1] <= y), None)
            print(f"  bottom {depth:.2f} m down: engine {e[2]:+.2f} m/s at +{e[0] - te:.3f} s | reference {r[2]:+.2f} m/s at +{r[0]:.3f} s"
                  if e and r else f"  bottom {depth:.2f} m down: engine {e} reference {r}")
        stone.update({"entry_speed": ve, "engine_floor_s": (floor_t - te) if floor_t else None, "reference_floor_s": ref_floor,
                      "reference_floor_speed": ref[-1][2] if ref else None})
    else:
        print("stone: no entry seen (samples:", len(trace), ")")

    # ── drop 2: the surface ──
    time.sleep(3)
    build_pond(api); time.sleep(5)
    api.debug("clear_dynamics"); time.sleep(0.3)
    dists = [0.0, 1 / 3, 2 / 3, 1.0, 4 / 3]
    api.debug("spawn_gpu_particle", {"x": DROP_X, "y": POND_TOP + args.height, "z": DROP_Z, "material": "Stone", "scale": edge, "lifetime": 30.0})
    surf, t0 = [], time.time()
    while time.time() - t0 < 4.0:
        row = [round(time.time() - t0, 4)]
        for d in dists:
            row.append(api.debug("water_av_probe", {"x": DROP_X + d, "y": 16.0, "z": DROP_Z}).get("surface_y"))
        surf.append(row)
    for j, d in enumerate(dists):
        vals = [(r[0], r[j + 1]) for r in surf if r[j + 1] is not None]
        if not vals: continue
        dev = [(t, y - REST) for t, y in vals]
        first = next((t for t, e in dev if abs(e) > 0.01), None)
        hi = max(dev, key=lambda p: p[1]); lo = min(dev, key=lambda p: p[1])
        print(f"surface {d:.2f} m: first |dev| > 1 cm at {first if first is None else round(first, 3)} s, max {hi[1]:+.3f} m at {hi[0]:.2f} s, min {lo[1]:+.3f} m at {lo[0]:.2f} s")
    Path(args.out).write_text(json.dumps({"stone": stone, "surface": {"dists": dists, "rows": surf}}, indent=1))
    print("wrote", args.out)


if __name__ == "__main__":
    main()
