#!/usr/bin/env python3
"""GRASS PACKING LAB — demonstrate whether grass blades overlap, visually and numerically.

Run BEFORE the non-overlap change to capture the baseline, and again after. The two runs are
directly comparable: same rig, same poses, same metric.

THE RIG
  A 5x5 Stone slab with ONE Grass voxel dead centre, held entirely inside chunk (-1,1,-1).
  ⚑A slab spanning x/z -2..2 straddles FOUR chunks and only one is resident in a fresh Flat world,
   so 21 of 25 fills drop SILENTLY (placed=4 failed=21, measured 2026-08-05) and the rig looks
   built but isn't. Every setup response is asserted by must().
  Wind off, day/night paused, camera fixed. One grass voxel means every green pixel is its blades.

THE METRIC — connected components of grass-coloured pixels
  Blades are opaque alpha-tested quads. Viewed from a steep angle at close range, N blades with
  well-separated roots resolve into ~N separate green strokes. Blades whose roots are piled on top
  of each other merge into ONE blob. So:

      components / blades drawn   ->  1.0 = every blade is individually visible
                                      0.2 = four out of five blades are hidden inside a neighbour

  This is a LOWER BOUND on separation, not a proof: two well-separated blades can still overlap
  from one viewpoint. It cannot replace the geometric unit test (min pairwise root distance vs
  blade width) — it is the human-visible companion to it. Read them together.

PREDICTION for the BEFORE run (state it so the run can falsify it):
  Blades are grouped into tufts of 7 (BLADES_PER_CLUMP) jittering inside a fixed +/-0.08u box,
  so components should saturate at roughly the number of TUFTS, not the number of blades:
  ratio ~1.0 at N=1..3, collapsing toward ~1/7 as N grows past one clump. If instead the ratio
  stays near 1.0 at N=28+, blades are NOT piling up and the premise of the whole change is wrong.

PASS 2 — EYE-HEIGHT DENSITY, the shot that actually decides the default
  The component metric above answers "do blades pile up". It does NOT answer "how many blades does
  turf need", because world-space separation and screen-space coverage are different things: from
  eye height at a grazing angle you see blades at many depths stacking up in SCREEN space, which is
  what makes turf read as solid. Blades only look like separated dots from overhead.

  So pass 2 plants the camera at 1.75u eye height on a LARGER grass patch and captures the same
  view at each candidate count, alongside the vertex cost. Spacing is `Cseq/sqrt(N)` (Cseq ~= 0.75
  for a progressive blue-noise ordering), and real lawn grass runs a spacing/width ratio of ~2-3x:

      N=20  sep 0.168u  4.2x width   360 verts/voxel
      N=30  sep 0.137u  3.4x width   540
      N=56  sep 0.100u  2.5x width  1008   <- the physically-grounded band
      N=90  sep 0.079u  2.0x width  1620
      N=140 sep 0.063u  1.6x width  2520   <- shipped

  Pick by looking, not by arithmetic. Cost falls ~linearly in N.

USAGE
  python tools/grass_packing_lab.py [--tag before] [--pass1-only | --pass2-only]
"""
import argparse, json, os, shutil, sys, time, urllib.request
import numpy as np
from PIL import Image

URL = "http://localhost:8090"
BLADE = (-3, 60, -3)          # centre of the slab, inside ONE chunk
SWEEP = (1, 2, 3, 7, 14, 28, 56, 112)
DENSITIES = (20, 30, 56, 90, 140)   # pass 2 candidates; 140 = shipped default
OUT = "docs/evidence"

# Pass 2 turf patch: a larger grass field so an eye-height view has grass running to a horizon
# rather than a 1-voxel tuft on a stone table. Kept inside chunk (-1,1,-1): world x/z -32..-1.
TURF = {"x1": -30, "z1": -30, "x2": -2, "z2": -2, "y": 60}
EYE = {"mode": "free",
       "position": {"x": -26.0, "y": 60 + 1 + 1.75, "z": -6.0},   # 1.75u eye height above the turf
       "yaw": -125.0, "pitch": -12.0}                              # grazing, looking across it

CSEQ = 0.75          # progressive blue-noise separation constant (Cseq/sqrt(N))
BLADE_W = 0.040      # authored boxy blade width, world units
VERTS_PER_BLADE = 18

# Steep-but-not-vertical view, close in: blades are vertical quads, so a true top-down view sees
# them edge-on and they vanish. ~55 degrees down keeps each blade a readable stroke while still
# separating roots across the voxel face.
CAM = {"mode": "free", "position": {"x": BLADE[0] + 1.6, "y": BLADE[1] + 2.4, "z": BLADE[2] + 1.6},
       "yaw": -135.0, "pitch": -38.0}


def post(p, d, timeout=60):
    r = urllib.request.Request(URL + p, data=json.dumps(d).encode(),
                               headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(r, timeout=timeout) as resp:
        b = resp.read().decode()
    return json.loads(b) if b.strip() else {}


def get(p, timeout=60):
    with urllib.request.urlopen(URL + p, timeout=timeout) as r:
        b = r.read().decode()
    return json.loads(b) if b.strip() else {}


def must(resp, what):
    """A discarded place_voxel response once silently emptied a rig and produced a whole table of
    zeros that looked like a real result. Assert on every setup call."""
    if isinstance(resp, dict) and resp.get("success") is False:
        raise SystemExit(f"SETUP FAILED: {what} -> {resp}")
    return resp


def exists(x, y, z):
    return bool(get(f"/api/world/voxel?x={x}&y={y}&z={z}").get("exists"))


def verify_filled(pts, what, tries=10):
    """/api/world/fill is ASYNC — it returns {"status":"accepted","async_id":N}, NOT a placed
    count, so the response says nothing about whether voxels landed. Verify the WORLD instead.
    ⚑A slab straddling four chunks silently drops the fills for the three that aren't resident,
     which leaves a rig that looks built but isn't; that is what this catches."""
    for _ in range(tries):
        missing = [p for p in pts if not exists(*p)]
        if not missing:
            return
        time.sleep(2)
    raise SystemExit(f"{what}: {len(missing)} of {len(pts)} voxels never appeared "
                     f"(e.g. {missing[:4]}) — refusing to measure an incomplete rig")


def build_rig():
    must(post("/api/world/fill", {"x1": -5, "y1": 60, "z1": -5, "x2": -1, "y2": 60, "z2": -1,
                                  "material": "Stone", "replace": True}), "slab")
    verify_filled([(x, 60, z) for x in range(-5, 0) for z in range(-5, 0)], "slab")
    post("/api/world/voxel/remove", {"x": BLADE[0], "y": BLADE[1], "z": BLADE[2]})
    time.sleep(1)
    must(post("/api/world/voxel", {"x": BLADE[0], "y": BLADE[1], "z": BLADE[2],
                                   "material": "Grass"}), "grass voxel")
    time.sleep(2)
    if not get(f"/api/world/voxel?x={BLADE[0]}&y={BLADE[1]}&z={BLADE[2]}").get("exists"):
        raise SystemExit("rig empty — refusing to measure")
    print(f"[rig] 5x5 Stone slab + ONE Grass voxel at {BLADE}, single chunk", flush=True)


def viewport(path):
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.int16)[40:670, 245:1195]


def shot(dst):
    shutil.copyfile(get("/api/screenshot")["path"], dst)
    return dst


def grass_mask(rgb):
    """DEPRECATED for blade counting — kept only for pass 2's coverage figure, where the whole
    ground is grass and the block face IS the thing being measured.

    ⚑It CANNOT isolate blades: the grass voxel's own top-face texture is green, so on the pass-1
     rig this mask returned ~51k px whether blades were drawn or not (control, 2026-08-05). Use
     blade_mask() for anything that counts blades."""
    r, g, b = rgb[:, :, 0], rgb[:, :, 1], rgb[:, :, 2]
    return (g > r + 12) & (g > b + 12)


def blade_mask(on, off, thresh=10):
    """Pixels the BLADES are responsible for: A/B the identical view with the blade layer enabled
    and disabled. The voxel's green top face, the stone, the sky and the lighting all cancel; what
    remains is blades. Same trick the shadow rig uses for cast/no-cast."""
    return np.abs(on.astype(np.int16) - off.astype(np.int16)).max(axis=2) > thresh


def components(mask, min_px=6):
    """4-connected component count via iterative flood fill (no scipy dependency).

    min_px drops antialiasing specks along blade edges, which would otherwise be counted as
    separate 'blades' and inflate the ratio."""
    h, w = mask.shape
    seen = np.zeros_like(mask, dtype=bool)
    n = 0
    ys, xs = np.nonzero(mask)
    for sy, sx in zip(ys, xs):
        if seen[sy, sx]:
            continue
        stack = [(sy, sx)]
        seen[sy, sx] = True
        size = 0
        while stack:
            y, x = stack.pop()
            size += 1
            for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                ny, nx = y + dy, x + dx
                if 0 <= ny < h and 0 <= nx < w and mask[ny, nx] and not seen[ny, nx]:
                    seen[ny, nx] = True
                    stack.append((ny, nx))
        if size >= min_px:
            n += 1
    return n


def build_turf():
    """Pass 2: a real patch of grass, so an eye-height view shows turf and not one tuft."""
    must(post("/api/world/fill", {"x1": TURF["x1"], "y1": TURF["y"], "z1": TURF["z1"],
                                  "x2": TURF["x2"], "y2": TURF["y"], "z2": TURF["z2"],
                                  "material": "Grass", "replace": True}), "turf")
    want = (TURF["x2"] - TURF["x1"] + 1) * (TURF["z2"] - TURF["z1"] + 1)
    # Sample the corners, edge midpoints and centre rather than all 841 — enough to catch the
    # "only one chunk was resident" failure, which drops whole quadrants, not scattered voxels.
    xs = (TURF["x1"], (TURF["x1"] + TURF["x2"]) // 2, TURF["x2"])
    zs = (TURF["z1"], (TURF["z1"] + TURF["z2"]) // 2, TURF["z2"])
    verify_filled([(x, TURF["y"], z) for x in xs for z in zs], "turf")
    print(f"[turf] {want} Grass voxels at y={TURF['y']}, single chunk", flush=True)
    return want


def pass2(tag):
    """Eye-height density comparison — the shot that decides the default blade count."""
    n_vox = build_turf()
    must(post("/api/camera", EYE), "eye camera")
    must(post("/api/debug/grass", {"enabled": True, "bladeHeight": 0.44, "bladeWidth": 1.0,
                                   "windStrength": 0.0}), "turf grass cfg")
    time.sleep(6)
    print(f"\nEYE-HEIGHT DENSITY  (camera y={EYE['position']['y']}, 1.75u above the turf)")
    print(f"{'blades':>7} {'sep~u':>7} {'sep/W':>7} {'verts/vox':>10} {'cover%':>8}", flush=True)
    rows = []
    for n in DENSITIES:
        must(post("/api/debug/grass", {"bladesPerVoxel": n}), f"density {n}")
        time.sleep(2.0)
        img = viewport(shot(f"{OUT}/turf_{tag}_n{n:03d}.png"))
        cover = 100.0 * float(grass_mask(img).sum()) / img[:, :, 0].size
        sep = CSEQ / (n ** 0.5)
        rows.append({"blades": n, "sep": round(sep, 4), "sep_over_w": round(sep / BLADE_W, 2),
                     "verts_per_voxel": VERTS_PER_BLADE * n, "cover_pct": round(cover, 2)})
        print(f"{n:>7} {sep:>7.3f} {sep/BLADE_W:>7.2f} {VERTS_PER_BLADE*n:>10} {cover:>8.2f}",
              flush=True)
    print("\ncover% is SCREEN coverage from eye height — the thing that decides whether the ground\n"
          "reads as turf. Look at turf_*_n*.png and pick; the numbers only rank the cost.")
    return {"voxels": n_vox, "camera": EYE, "rows": rows}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tag", default="before", help="before | after — names the captures")
    ap.add_argument("--pass1-only", action="store_true", help="overlap metric only")
    ap.add_argument("--pass2-only", action="store_true", help="eye-height density only")
    args = ap.parse_args()

    for _ in range(60):
        try:
            get("/api/state"); break
        except Exception:
            time.sleep(3)

    os.makedirs(OUT, exist_ok=True)
    must(post("/api/debug/shadow", {"mode": 0}), "normal render")
    must(post("/api/daynight/set", {"enabled": True, "paused": True, "timeOfDay": 12.0}), "sun")

    if args.pass2_only:
        d = pass2(args.tag)
        with io_open(f"{OUT}/turf_{args.tag}.json") as f:
            json.dump({"tag": args.tag, "pass2": d}, f, indent=2)
        print(f"wrote {OUT}/turf_{args.tag}.json")
        return 0

    build_rig()
    must(post("/api/debug/shadow", {"mode": 0}), "normal render")
    must(post("/api/daynight/set", {"enabled": True, "paused": True, "timeOfDay": 12.0}), "sun")
    must(post("/api/camera", CAM), "camera")
    must(post("/api/debug/grass", {"enabled": True, "bladeHeight": 1.2, "bladeWidth": 1.0,
                                   "windStrength": 0.0}), "grass cfg")
    time.sleep(6)

    print(f"\ncamera {CAM['position']} yaw {CAM['yaw']} pitch {CAM['pitch']}   sun pinned at noon")
    print("blades = drawn per voxel · comps = separate green regions · ratio = comps/blades\n")
    print(f"{'blades':>7} {'px':>8} {'comps':>7} {'ratio':>7}  reading", flush=True)

    def blades_ab(n, tag):
        """Capture the identical view with the blade layer on and off; the difference IS the blades."""
        must(post("/api/debug/grass", {"enabled": True, "bladesPerVoxel": n}), f"blades {n}")
        time.sleep(1.6)
        on = viewport(shot(f"{OUT}/pack_{tag}_on.png"))
        must(post("/api/debug/grass", {"enabled": False}), "blades off")
        time.sleep(1.6)
        off = viewport(shot(f"{OUT}/pack_{tag}_off.png"))
        return blade_mask(on, off)

    rows = []
    for n in SWEEP:
        m = blades_ab(n, f"{args.tag}_n{n:03d}")
        px = int(m.sum())
        c = components(m) if px else 0
        ratio = (c / n) if n else 0.0
        reading = ("all blades distinct" if ratio > 0.85 else
                   "some merging" if ratio > 0.45 else
                   "HEAVILY MERGED")
        rows.append({"blades": n, "px": px, "components": c, "ratio": round(ratio, 3)})
        print(f"{n:>7} {px:>8} {c:>7} {ratio:>7.2f}  {reading}", flush=True)

    # CONTROL: remove the grass VOXEL, so there are no blades to draw at all. Toggling the blade
    # layer must then change essentially nothing. If it does change, the A/B is picking up
    # something other than blades and every number above is meaningless — which is exactly what
    # the first version of this metric did (it was reading the voxel's own green top face).
    must(post("/api/world/voxel/remove", {"x": BLADE[0], "y": BLADE[1], "z": BLADE[2]}), "rm grass")
    time.sleep(3)
    ctrl = int(blades_ab(112, f"{args.tag}_control").sum())
    must(post("/api/world/voxel", {"x": BLADE[0], "y": BLADE[1], "z": BLADE[2],
                                   "material": "Grass"}), "restore grass voxel")
    must(post("/api/debug/grass", {"enabled": True, "bladesPerVoxel": 140}), "restore")
    print(f"\nCONTROL (grass voxel removed): {ctrl} px change  "
          f"{'PASS' if ctrl < 200 else 'FAIL — the A/B is catching something that is not blades'}")

    out = {"tag": args.tag, "camera": CAM, "control_px": ctrl, "rows": rows}
    if not args.pass1_only:
        out["pass2"] = pass2(args.tag)
    with io_open(f"{OUT}/pack_{args.tag}.json") as f:
        json.dump(out, f, indent=2)
    print(f"wrote {OUT}/pack_{args.tag}.json  and  {OUT}/pack_{args.tag}_n*.png")
    return 0


def io_open(p):
    return open(p, "w", encoding="utf-8")


if __name__ == "__main__":
    sys.exit(main())
