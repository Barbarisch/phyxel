"""lighting_gate_rig.py — build and measure the lighting gates in a CONTROLLED world.

docs/UnifiedLightingPlan.md D2. M2 and M3 were originally gated in the CharacterTestbed project,
which is populated and uncontrolled. That directly produced:

  * a DEAD CONTROL — debug mode 5 read the same luminance with the light REMOVED, because the
    measured region was grass and foliage, which render normally in every debug mode;
  * probes buried inside terrain, because a surface height was assumed rather than queried;
  * rig voxels SILENTLY REFUSED because terrain already occupied the cells, so the rig was not
    the shape it was specified to be.

So this script does three things the earlier ad-hoc rigs did not:
  1. builds into a flat, vegetation-free world (the LightingLab project);
  2. REPORTS placed-vs-refused for every batch and REFUSES TO MEASURE if anything was refused —
     a rig that is not the specified shape must not silently become the evidence;
  3. queries every surface height instead of assuming it.

Usage:
    python tools/lighting_gate_rig.py [--port 8090]

Requires a running engine on the LightingLab project.
"""
import argparse
import json
import sys
import urllib.request

BASE = "http://localhost:8090"


def get(path, timeout=30):
    return json.load(urllib.request.urlopen(BASE + path, timeout=timeout))


def post(path, body, timeout=120):
    req = urllib.request.Request(BASE + path, json.dumps(body).encode(),
                                 {"Content-Type": "application/json"})
    raw = urllib.request.urlopen(req, timeout=timeout).read().decode()
    return json.loads(raw) if raw else {}


def place(voxels, what):
    """Place a batch and FAIL LOUDLY on any refusal. `/api/world/fill` is async and reports no
    placed count at all, and the batch path reports refusals that are easy to ignore — ignoring
    them is exactly how a rig silently stops being the shape it was specified to be."""
    r = post("/api/world/voxel/batch", {"voxels": voxels})
    placed, failed = r.get("placed", 0), r.get("failed", 0)
    print(f"    {what}: placed {placed}, refused {failed}")
    if failed:
        raise SystemExit(
            f"ABORT: {failed} of {len(voxels)} voxels refused building '{what}'. The rig is not "
            f"the specified shape, so any measurement taken on it would be evidence about "
            f"something else. Fix the world or the placement before measuring.")
    return placed


def surface_y(x, z):
    """Query, never assume. A probe placed at an assumed height was buried 16 voxels inside solid
    ground and read as a defect."""
    return get(f"/api/world/terrain_height?x={x}&z={z}")["surface_y"]


def sky(x, y, z):
    return get(f"/api/debug/light_occupancy?sky_probe=1&x={x}&y={y}&z={z}")["sky_probe"][
        "sky_visibility"]


def vis(sx, sy, sz, lx, ly, lz, nx=0.0, ny=1.0, nz=0.0):
    q = (f"/api/debug/light_occupancy?x={sx}&y={sy}&z={sz}"
         f"&lx={lx}&ly={ly}&lz={lz}&nx={nx}&ny={ny}&nz={nz}")
    return get(q)["visibility"]


def box(lo, hi, material="Stone", doorway=None):
    """Shell over [lo,hi] inclusive. `doorway` = (y0, y1, z0, z1) removed from the -X wall."""
    out = []
    for x in range(lo[0], hi[0] + 1):
        for y in range(lo[1], hi[1] + 1):
            for z in range(lo[2], hi[2] + 1):
                on_shell = (x in (lo[0], hi[0]) or y in (lo[1], hi[1]) or z in (lo[2], hi[2]))
                if not on_shell:
                    continue
                if doorway and x == lo[0]:
                    y0, y1, z0, z1 = doorway
                    if y0 <= y <= y1 and z0 <= z <= z1:
                        continue
                out.append({"x": x, "y": y, "z": z, "material": material})
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8090)
    args = ap.parse_args()
    global BASE
    BASE = f"http://localhost:{args.port}"

    occ = get("/api/debug/light_occupancy")
    print(f"world: {occ['resident_chunks']}/{occ['loaded_chunks']} chunks resident, "
          f"{occ['mixed_cubes']} mixed cubes, out-of-box {occ['out_of_box_chunks']}")
    if not occ["ready"]:
        raise SystemExit("ABORT: light occupancy is not resident; nothing can be measured.")

    gy = surface_y(20, 20)
    floor = gy + 1                      # first air cell above the ground
    print(f"ground surface queried at (20,20): y={gy}; building on y={floor}\n")

    print("building rigs (any refusal aborts):")
    # Sealed box and doorway box, well separated so neither can influence the other.
    place(box((16, floor, 16), (24, floor + 6, 24)), "sealed box")
    place(box((36, floor, 16), (44, floor + 6, 24),
              doorway=(floor + 1, floor + 2, 19, 21)), "doorway box")

    inside_sealed = (20.5, float(floor + 1), 20.5)
    inside_door_near = (36.5, float(floor + 1), 20.5)   # on the threshold
    inside_door_far = (43.5, float(floor + 1), 20.5)
    open_ground = (60.5, float(gy + 1), 60.5)

    print("\n=== M3 GATE — sky as an emitter ===")
    v_sealed = sky(*inside_sealed)
    v_near = sky(*inside_door_near)
    v_far = sky(*inside_door_far)
    v_open = sky(*open_ground)
    print(f"  sealed interior      : {v_sealed:.4f}   expect 0")
    print(f"  doorway, at threshold: {v_near:.4f}   expect > 0")
    print(f"  doorway, far corner  : {v_far:.4f}   expect < threshold")
    print(f"  open ground          : {v_open:.4f}   expect 1")
    m3 = (v_sealed == 0.0) and (v_near > 0.0) and (v_near > v_far) and (v_open > 0.99)
    print(f"  M3: {'PASS' if m3 else 'FAIL'}  (sealed < opening < open, with falloff)")

    print("\n=== M2 GATE — point-light visibility ===")
    # Light sealed INSIDE the box; every exterior face must be dark. Control: same light outside.
    lx, ly, lz = 20.5, float(floor + 3), 20.5
    faces = {
        "-X": ((15.5, float(floor + 3), 20.5), (1, 0, 0)),
        "+X": ((25.5, float(floor + 3), 20.5), (-1, 0, 0)),
        "-Z": ((20.5, float(floor + 3), 15.5), (0, 0, 1)),
        "+Z": ((20.5, float(floor + 3), 25.5), (0, 0, -1)),
        "roof": ((20.5, float(floor + 7), 20.5), (0, 1, 0)),
        "-X-Z corner": ((15.5, float(floor + 3), 15.5), (1, 0, 1)),
    }
    leaked = []
    for name, (p, n) in faces.items():
        r = vis(p[0], p[1], p[2], lx, ly, lz, n[0], n[1], n[2])
        if r["visible"]:
            leaked.append(name)
        print(f"  outside {name:<12}: {'LIT (leak)' if r['visible'] else 'dark'}")

    ctrl_p, ctrl_n = faces["-X"]
    ctrl = vis(ctrl_p[0], ctrl_p[1], ctrl_p[2], 13.0, float(floor + 3), 20.5,
               ctrl_n[0], ctrl_n[1], ctrl_n[2])
    print(f"  CONTROL (light outside, same face): "
          f"{'lit — control OK' if ctrl['visible'] else 'dark — CONTROL DEAD'}")
    m2 = (not leaked) and ctrl["visible"]
    print(f"  M2: {'PASS' if m2 else 'FAIL'}"
          + (f"  leaked at {leaked}" if leaked else "")
          + ("" if ctrl["visible"] else "  (a zero with a dead control is not a result)"))

    print(f"\nOVERALL: {'PASS' if (m2 and m3) else 'FAIL'}")
    return 0 if (m2 and m3) else 1


if __name__ == "__main__":
    sys.exit(main())
