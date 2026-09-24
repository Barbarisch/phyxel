#!/usr/bin/env python3
"""Glass transparency Phase 0 - measure TRANSMISSION, with a control.
docs/GlassTransparency.md 2 and 7.

    python tools/glass_transmission.py

WHY A NUMBER AND NOT A SCREENSHOT. This defect has been declared fixed twice by looking at pixels,
and was wrong both times - once because a milky pane with the horizon faintly visible through it was
read as "transparent". "Does it look see-through" is not a question a capture can answer. "Does what
is behind the pane change what you see through it" is.

THE MEASUREMENT. Swap the backdrop between two materials and measure how much of that change
survives the pane, divided by the same swap measured with NO pane in the way:

    T = (P_a - P_b) / (C_a - C_b)

where P is the pixel patch seen through the pane and C the same patch in the control capture.

T IS 1 - materialAlpha, NOT materialAlpha. Blending gives P = alpha*G + (1-alpha)*C, so the G term
(the pane's own lit colour) CANCELS in the subtraction and the ratio is (1-alpha). Glass declares
alpha 0.5, which is its own complement - so the expected T is 0.5 either way and that coincidence
hid a wrong derivation in the first draft of the plan. At any other alpha the prediction is
1 - alpha.

    T ~ 0.5  working
    T ~ 0    opaque - the backdrop is not reaching the camera at all
    0 < T < 0.5  milky but blending - a DIFFERENT defect from opacity, and the one that was
                 previously mistaken for success

THE BACKDROP PAIR, and why it is NOT emissive (the plan said it would be, and the plan was wrong).
The design-check chose `glow` vs `glow_blue` because emissive fragments are flat and unshadowed.
Measured, that fails twice over:
  1. At the default exposure with AgX off both backdrops CLIP to (255,255,255). A clipped pixel
     carries no colour, so the control delta was exactly 0.0 and no ratio existed.
  2. Worse, `glow`, `glow_blue` and `glow_green` all reference the SAME texture files in
     materials.json and carry no colour or tint field - they are visually identical materials. The
     colour in their names does not exist at the material level. Swept across exposure 4.0 -> 0.1
     they never differed by more than 2.4/255, i.e. noise.
So the backdrop is two ORDINARY materials with strongly different hue (Bricks red vs Ice pale blue).

THE SHADOW CONFOUND IS THEN BOUNDED BY MEASUREMENT, NOT ARGUMENT. A pane that shadows its own
backdrop turns the ratio into (1-alpha)*s, and s does not cancel. Rather than reason about whether
a vertical pane shadows a parallel wall behind it, the rig measures an OPAQUE-PANE FLOOR: the same
scene with a Stone pane, where the backdrop genuinely cannot be seen and T must read ~0. That is the
empirical zero-point. If Stone reads ~0 and glass reads ~0.5, the measurement discriminates
regardless of what s is - the same role the pristine-vs-pristine noise floor played in the damage
stage-count rig, where a 2.27% floor was the difference between "faint cracking" and "nothing".

TWO ARMS, because they are predicted to differ. A full-cube glass pane and a SUB-VOXEL (subcube)
one. Chunk::recomputeRenderFlags scanned only the cube store, so before the fix a chunk whose only
glass was sub-voxel reported hasTransparentVoxel() == false and the whole OIT pass was skipped for
the frame. Generated windows are sub-voxel, so that arm is the one that matters for real buildings.
"""

import argparse
import json
import sys
import time
import urllib.error
import urllib.request

BASE = "http://localhost:8090"

# One chunk (origin 0,0,0) holds all of this, so no fill can drop silently on a non-resident chunk.
PANE_X0, PANE_X1 = 8, 11
PANE_Y0, PANE_Y1 = 17, 20
PANE_Z = 8
BACK_X0, BACK_X1 = 6, 13          # wider than the pane, so the pane is fully backed
BACK_Y0, BACK_Y1 = 15, 22
BACK_Z = 4
CAM = {"x": 9.5, "y": 18.5, "z": 20.0}

# Strongly different hue. NOT the emissive pair -- see the module docstring.
BACKDROPS = [("a", "Bricks"), ("b", "Ice")]

# AgX off (curve 0) keeps the ratio linear, but the shipped exposure of 8 then clips bright
# surfaces to 255 and destroys the colour the ratio depends on. 1.0 keeps the patch well clear of
# the ceiling; the rig asserts that rather than trusting it.
EXPOSURE = 1.0
CLIP_CEILING = 250


# Endpoints a historical build turned out not to have. Reported in the RESULT line, because a
# number measured without (say) the tonemap control is measured under different conditions and the
# bisect table must say so.
MISSING = set()
SHOT_ROOT = None   # engine working directory, when it differs from this script's (--shot-root)


def call(path, body=None, timeout=120):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(BASE + path, data=data,
                                 method="POST" if data else "GET",
                                 headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            raw = r.read().decode()
            try:
                return json.loads(raw) if raw.strip() else {}
            except ValueError:
                return {}
    except urllib.error.HTTPError as e:
        # HTTPError SUBCLASSES URLError, so it must be caught first. An old build answering 404 is
        # reachable -- it just predates that endpoint. Treating it as "unreachable" would abort the
        # bisect step on a missing debug knob.
        MISSING.add(path.split("?")[0])
        return {"_http_status": e.code}
    except urllib.error.URLError as e:
        sys.exit("engine API unreachable (%s)" % e)


def job(kind, params):
    j = call("/api/job/submit", {"type": kind, "params": params})
    jid = j.get("job_id")
    for _ in range(120):
        if call("/api/job/%s" % jid).get("state") in ("complete", "failed"):
            return
        time.sleep(0.25)


def clear_all():
    job("clear_region", {"x1": BACK_X0 - 2, "y1": 14, "z1": BACK_Z - 1,
                         "x2": BACK_X1 + 2, "y2": BACK_Y1 + 2, "z2": PANE_Z + 1})


def fill(x0, y0, z0, x1, y1, z1, material):
    call("/api/world/fill", {"x1": x0, "y1": y0, "z1": z0,
                             "x2": x1, "y2": y1, "z2": z1, "material": material})


def build_backdrop(material):
    fill(BACK_X0, BACK_Y0, BACK_Z, BACK_X1, BACK_Y1, BACK_Z, material)


def build_pane(kind):
    """kind: 'cube' | 'subcube' | None. Returns True if the pane is actually there."""
    if kind is None:
        return True
    if kind in ("cube", "opaque"):
        fill(PANE_X0, PANE_Y0, PANE_Z, PANE_X1, PANE_Y1, PANE_Z,
             "Stone" if kind == "opaque" else "Glass")
        time.sleep(2.5)
        want = "Stone" if kind == "opaque" else "Glass"
        v = call("/api/world/voxel?x=%d&y=%d&z=%d" % (PANE_X0, PANE_Y0, PANE_Z))
        # Older builds' voxel query may not report "material"; then existence is all that can be
        # verified, and the RESULT line records the weaker check.
        if "material" not in v:
            MISSING.add("voxel.material")
            return bool(v.get("exists"))
        return bool(v.get("exists")) and v.get("material") == want
    # Sub-voxel arm: the same slab built from glass SUBCUBES (3x3x3 per cube cell = 432 of them).
    # ONE batch call, not 432 HTTP round-trips -- /api/world/subcubes/batch exists precisely for
    # this and the per-call form takes minutes.
    subs = [{"x": x, "y": y, "z": PANE_Z, "sx": sx, "sy": sy, "sz": sz, "material": "Glass"}
            for x in range(PANE_X0, PANE_X1 + 1)
            for y in range(PANE_Y0, PANE_Y1 + 1)
            for sx in range(3) for sy in range(3) for sz in range(3)]
    r = call("/api/world/subcubes/batch", {"subcubes": subs}, timeout=180)
    placed = r.get("placed", 0)
    time.sleep(2.5)
    return placed > 0


def patch_mean(path):
    """Mean RGB over a fixed rectangle centred on the pane, in the viewport.

    A rectangle, not per-pixel: the two emissive backdrops carry different texture patterns, and
    those only cancel in the mean.
    """
    import os
    from PIL import Image
    # Relative screenshot paths are relative to the ENGINE's working directory, not this script's.
    # For a historical build run from a worktree those differ -- resolving against os.getcwd() read
    # a file that did not exist on the first gate attempt.
    full = path if os.path.isabs(path) else os.path.join(SHOT_ROOT or os.getcwd(), path)
    im = Image.open(full).convert("RGB")
    W, H = im.size
    px = im.load()
    # Viewport centre; the editor docks panels left/right, so stay well inside.
    cx, cy = W // 2, H // 2 - 60
    half = 60
    n = 0
    mx = 0
    acc = [0.0, 0.0, 0.0]
    for x in range(cx - half, cx + half):
        for y in range(cy - half, cy + half):
            r, g, b = px[x, y]
            acc[0] += r; acc[1] += g; acc[2] += b
            mx = max(mx, r, g, b)
            n += 1
    return [c / n for c in acc], mx


def capture():
    time.sleep(1.2)
    mean, mx = patch_mean(call("/api/screenshot").get("path", ""))
    if mx >= CLIP_CEILING:
        sys.exit("CLIPPED: the sampled patch peaks at %d/255. A saturated pixel carries no colour, "
                 "so the ratio below it is meaningless -- this is exactly how the first run "
                 "measured a control delta of 0.0. Lower EXPOSURE (currently %.2f)." % (mx, EXPOSURE))
    return mean


def run_arm(label, pane_kind):
    """Returns (T, detail) for one pane kind (or the control when pane_kind is None)."""
    means = {}
    for tag, material in BACKDROPS:
        clear_all()
        build_backdrop(material)
        time.sleep(2.0)
        if not build_pane(pane_kind):
            return None, "pane (%s) did not build" % pane_kind
        call("/api/camera", {"position": CAM, "yaw": -90, "pitch": 0})
        means[tag] = capture()
    delta = [means["a"][i] - means["b"][i] for i in range(3)]
    return delta, means


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--arms", default="opaque,cube,subcube",
                    help="pane kinds to measure (default: opaque,cube,subcube -- opaque is the FLOOR)")
    ap.add_argument("--port", type=int, default=8090)
    ap.add_argument("--label", default="HEAD", help="commit label for the RESULT line")
    ap.add_argument("--shot-root", default=None,
                    help="the ENGINE's working directory, if not this one (historical worktrees)")
    ap.add_argument("--target", type=float, default=None,
                    help="decided transmission target (docs/GlassTransparency.md 13.6a: 0.80). Adds "
                         "an ON/OFF-TARGET verdict for a +/-0.05 band. Without it the per-arm label "
                         "is centred on 0.5 and mislabels anything else (14.2).")
    ap.add_argument("--ambient", type=float, default=None,
                    help="set_ambient strength before measuring (historical builds lack the tonemap "
                         "control, so dimming is the only way to keep the patch unclipped)")
    ap.add_argument("--gi-off", action="store_true",
                    help="switch the ambient probe field off for the run (diagnostic: separates "
                         "coverage from glass occluding the backdrop's AMBIENT light)")
    ap.add_argument("--time-of-day", type=float, default=None,
                    help="daynight_set timeOfDay, with timeScale 0 so light cannot drift between arms")
    args = ap.parse_args()
    global BASE, SHOT_ROOT
    BASE = "http://localhost:%d" % args.port
    SHOT_ROOT = args.shot_root

    print("Glass transmission (Phase 0)")
    print("expected T = 1 - alpha = 0.50 for Glass (alpha 0.50)")
    print("")

    call("/api/world/generate", {"type": "Flat", "from": {"x": 0, "y": 0, "z": 0},
                                 "to": {"x": 1, "y": 0, "z": 0}})
    call("/api/debug/tonemap", {"curve": 0, "exposure": EXPOSURE})
    # Lighting is held FIXED across every arm. T is a ratio against a control captured under the
    # same light, so the light LEVEL cancels -- but only if it does not change between captures.
    # A running day cycle would change it, so time is frozen whenever it is set.
    conditions = []
    if args.ambient is not None:
        call("/api/ambient", {"strength": args.ambient})
        conditions.append("ambient=%.2f" % args.ambient)
    if args.gi_off:
        call("/api/debug/gi", {"enabled": False})
        conditions.append("gi-off")
    if args.time_of_day is not None:
        call("/api/daynight/set", {"timeOfDay": args.time_of_day, "timeScale": 0.0})
        conditions.append("tod=%.2f" % args.time_of_day)
    if "/api/debug/tonemap" in MISSING:
        conditions.append("NO-TONEMAP-CONTROL(engine default curve)")

    # CONTROL FIRST. Without the no-pane backdrop swap there is no denominator, and every number
    # below would be an absolute brightness that means nothing.
    ctl_delta, ctl_means = run_arm("control", None)
    if ctl_delta is None:
        print("RESULT %s UNTESTABLE control-did-not-build missing=%s"
              % (args.label, ",".join(sorted(MISSING)) or "-"))
        return 3
    ctl_mag = sum(abs(c) for c in ctl_delta)
    print("control (no pane): backdrop swap moved the patch by RGB %s  |sum| = %.1f"
          % (["%.1f" % c for c in ctl_delta], ctl_mag))
    if ctl_mag < 12.0:
        # UNTESTABLE, never BAD: if the backdrop cannot be seen with NO pane, nothing about the pane
        # can be concluded. Counting this as "glass opaque" would poison the bisect.
        print("CONTROL FAILED: swapping the backdrop barely changed the picture (|sum| %.1f). The "
              "backdrop is not visible to the camera, so no transmission can be measured." % ctl_mag)
        print("RESULT %s UNTESTABLE control=%.1f missing=%s"
              % (args.label, ctl_mag, ",".join(sorted(MISSING)) or "-"))
        return 3
    print("")

    results = {}
    for kind in [a.strip() for a in args.arms.split(",") if a.strip()]:
        delta, detail = run_arm(kind, kind)
        if delta is None:
            print("%-9s  --  %s" % (kind, detail))
            results[kind] = None
            continue
        t = sum(abs(d) for d in delta) / ctl_mag
        results[kind] = t
        verdict = ("OPAQUE" if t < 0.08 else
                   "working" if 0.35 <= t <= 0.65 else
                   "partial/milky")
        if kind == "opaque":
            verdict += "  <- FLOOR: a Stone pane. Anything at this level is not transmitting."
        print("%-9s  T = %.3f   (%s)   raw RGB delta %s"
              % (kind, t, verdict, ["%.1f" % d for d in delta]))

    call("/api/debug/tonemap", {"curve": 1, "exposure": 8.0})
    if args.gi_off:
        call("/api/debug/gi", {"enabled": True})

    # Classification per 12.8 step 4. PARTIAL is recorded and investigated, never forced.
    cube = results.get("cube")
    if cube is None:
        cls = "UNTESTABLE"
    elif cube >= 0.25:
        cls = "GOOD"
    elif cube <= 0.08:
        cls = "BAD"
    else:
        cls = "PARTIAL"
    fmt = lambda v: "--" if v is None else "%.3f" % v
    print("")
    print("RESULT %s %s cube=%s subcube=%s floor=%s control=%.1f missing=%s conditions=%s"
          % (args.label, cls, fmt(cube), fmt(results.get("subcube")), fmt(results.get("opaque")),
             ctl_mag, ",".join(sorted(MISSING)) or "-", ";".join(conditions) or "shipped"))
    if args.target is not None:
        for arm in ("cube", "subcube"):
            v = results.get(arm)
            if v is None:
                continue
            on = abs(v - args.target) <= 0.05
            print("TARGET %s %s T=%.3f target=%.2f band=+/-0.05"
                  % (arm, "ON-TARGET" if on else "OFF-TARGET", v, args.target))
    print("tonemap restored to the shipped curve (where the endpoint exists)")
    print("NOTE: captured with debug tonemap curve 0 (AgX off). Through the shipped curve the same")
    print("      T reads compressed - state the curve with every number.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

