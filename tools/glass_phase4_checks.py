#!/usr/bin/env python3
"""Glass Phase 4 L4 checks - docs/GlassTransparency.md 13.9, 13.12, 13.15, 13.17.

    python tools/glass_phase4_checks.py shadow   # 13.9  glass casts no shadow
    python tools/glass_phase4_checks.py crack    # 13.15 cracks visible on glass, frosted
    python tools/glass_phase4_checks.py far      # 13.12 crack seed exact far from the origin
    python tools/glass_phase4_checks.py tint     # 13.17 the pane shows GLASS's texture (run with a
                                                 #       temporarily coloured glass texture swapped in)

Transmission itself (T in the decided 0.75-0.85 band) is tools/glass_transmission.py --target 0.80.

Every check follows the rig discipline in docs/FeatureDesignKeys.md: one chunk where possible, one
variable, a CONTROL that proves the metric sees what it claims, a FLOOR or ratio so a noise reading
cannot pass for a signal, and a prediction printed before the numbers. Each ends with one
machine-readable RESULT line.

Conditions shared with glass_transmission.py: tonemap curve 0 (AgX off), exposure 1.0, and a patch
that must stay unclipped (the helpers exit loudly otherwise).
"""

import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import glass_transmission as gt   # noqa: E402  (shared call/fill/job/capture helpers)

GROUND_TOP = 17   # Flat world: grass blocks at y=16, top face at y=17


def lum(rgb):
    return 0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2]


def cam(x, y, z, yaw=-90.0, pitch=0.0):
    gt.call("/api/camera", {"position": {"x": x, "y": y, "z": z}, "yaw": yaw, "pitch": pitch})


def voxel(x, y, z):
    return gt.call("/api/world/voxel?x=%d&y=%d&z=%d" % (x, y, z))


def wait_voxel(x, y, z, material=None, timeout=45.0):
    """Poll until a voxel exists (and has `material`), instead of sleeping a fixed time.

    Fills are ASYNCHRONOUS and their latency is not constant: near the origin a 4x4 fill lands in
    ~2 s, but at x ~ 100 000 it took 17.3 s. The first far runs slept a fixed 3 s and reported the
    pane as "did not build" -- a rig timing bug that looked exactly like an engine coordinate limit.
    """
    t0 = time.time()
    while time.time() - t0 < timeout:
        v = voxel(x, y, z)
        if v.get("exists") and (material is None or v.get("material") == material):
            return v
        time.sleep(0.5)
    return {}


def clear(x1, y1, z1, x2, y2, z2):
    gt.job("clear_region", {"x1": x1, "y1": y1, "z1": z1, "x2": x2, "y2": y2, "z2": z2})


def setup_common():
    gt.call("/api/world/generate", {"type": "Flat", "from": {"x": 0, "y": 0, "z": 0},
                                    "to": {"x": 1, "y": 0, "z": 0}})
    gt.call("/api/debug/tonemap", {"curve": 0, "exposure": gt.EXPOSURE})


def restore_common():
    gt.call("/api/debug/tonemap", {"curve": 1, "exposure": 8.0})


# ------------------------------------------------------------------------------------ 13.9 shadow
def check_shadow():
    """Glass casts NO shadow.

    A horizontal ROOF over open ground, sun at noon so the roof's shadow falls straight under it.
    The camera sits beside the roof, below its height, looking down at the ground UNDER it -- it sees
    the roof's shadow, never the roof. Three arms at one pose: no roof, Stone roof, Glass roof.

        R = (ground_glass - ground_stone) / (ground_none - ground_stone)

    R ~ 1: glass casts no shadow.  R ~ 0: glass shadows like stone.  Stone is the CONTROL that the
    shadow is really there; if none - stone is small the sun is not reaching under the roof and the
    check is UNTESTABLE, never a pass.

    GI (the probe field) is switched OFF for the measurement: a roof also blocks SKY AMBIENT, and the
    requirement being tested is the sun shadow map (13.9). Whether glass should occlude ambient in
    the probe field is a separate question, recorded in the plan, not silently folded in here.
    """
    print("CHECK shadow (13.9) - prediction: R >= 0.90 after the fix; ~0 before it")
    setup_common()
    gt.call("/api/daynight/set", {"timeOfDay": 12.0, "timeScale": 0.0})
    gt.call("/api/debug/gi", {"enabled": False})
    RX0, RX1, RZ0, RZ1, RY = 5, 14, 1, 12, 23
    # The camera looks at the rig's own floor plate (top face y=18). Ray from y=20 at pitch -12
    # meets it at z ~ 7.6: under the roof's centre, while the roof itself stays out of frame.
    pose = (9.5, 20.0, 17.0, -90.0, -12.0)

    # THE RIG BRINGS ITS OWN GROUND (15.7). On the first run the Stone roof darkened the ground by
    # only 3.1 because the world's ground renders BLACK in this project at these settings -- a
    # shadow cannot darken black. A placed Sand plate renders lit (~94/89/76) with GI on or off, so
    # it is the surface the shadow is measured on. Built once; the arms change ONLY the roof.
    clear(1, GROUND_TOP, -3, 18, RY + 3, 16)
    gt.fill(2, GROUND_TOP, -2, 17, GROUND_TOP, 15, "Sand")
    time.sleep(3.0)
    if not voxel(9, GROUND_TOP, 7).get("exists"):
        print("RESULT shadow UNTESTABLE floor-plate-did-not-build")
        return 3

    ground = {}
    for arm, mat in (("none", None), ("stone", "Stone"), ("glass", "Glass")):
        clear(RX0 - 2, RY - 1, RZ0 - 2, RX1 + 2, RY + 2, RZ1 + 2)
        if mat:
            gt.fill(RX0, RY, RZ0, RX1, RY, RZ1, mat)
            time.sleep(2.5)
            v = voxel(RX0 + 2, RY, RZ0 + 2)
            if not v.get("exists"):
                print("RESULT shadow UNTESTABLE %s-roof-did-not-build" % arm)
                return 3
        time.sleep(1.5)
        cam(*pose)
        ground[arm] = lum(gt.capture())
        print("  ground under roof, %-5s roof: luminance %.1f" % (arm, ground[arm]))

    gt.call("/api/debug/gi", {"enabled": True})
    gt.call("/api/daynight/set", {"timeScale": 1.0})
    restore_common()

    span = ground["none"] - ground["stone"]
    if span < 8.0:
        print("RESULT shadow UNTESTABLE control-span=%.1f (the Stone roof barely darkens the ground: "
              "the sun is not reaching under the roof)" % span)
        return 3
    r = (ground["glass"] - ground["stone"]) / span
    verdict = "PASS" if r >= 0.90 else "FAIL"
    print("RESULT shadow %s R=%.3f none=%.1f stone=%.1f glass=%.1f conditions=noon,gi-off"
          % (verdict, r, ground["none"], ground["stone"], ground["glass"]))
    return 0 if verdict == "PASS" else 1


# ------------------------------------------------------------------------------------ 13.15 crack
def check_crack(base_x=0, label="crack"):
    """Cracks are VISIBLE on glass, and FROSTED (brighter), against a floor.

    REDESIGNED after the first red run (GlassTransparency.md 15.7): comparing two side-by-side panes
    meant moving the camera between them, and two CLEAN panes already differed by |RGB| 130 -- the
    camera move, not the damage, dominated. Now ONE pane, ONE fixed camera, and the only thing that
    changes is the damage:

        floor = |clean capture 1 - clean capture 2|     (capture-to-capture noise, nothing changed)
        diff  = |damaged capture - clean capture 1|

    Full-cube glass, because damage accumulation is cube-only (sub-voxel damage is V2,
    VoxelDamageVisualization.md 3.6). Damage 0.45 of Glass's toughness (stage 3 of 7), radius 0.4,
    uniformity asserted.
    """
    P0 = base_x + 8
    print("CHECK %s (13.15) - prediction: diff > 2 x floor, damaged pane BRIGHTER" % label)
    clear(base_x + 4, GROUND_TOP, 3, base_x + 15, 26, 10)
    gt.fill(base_x + 6, 15, 4, base_x + 13, 22, 4, "Bricks")
    gt.fill(P0, 17, 8, P0 + 3, 20, 8, "Glass")
    wait_voxel(base_x + 12, 21, 4, "Bricks")            # backdrop's last corner
    v = wait_voxel(P0 + 3, 20, 8, "Glass")              # pane's last corner
    time.sleep(1.5)                                     # one more remesh before capturing
    if not (v.get("exists") and v.get("material") == "Glass"):
        print("RESULT %s UNTESTABLE pane-did-not-build" % label)
        return 3, None

    cam(P0 + 2.0, 18.5, 20.0)
    clean1 = gt.capture()
    clean2 = gt.capture()
    floor = sum(abs(clean1[i] - clean2[i]) for i in range(3))
    print("  floor (clean vs clean, same camera): |RGB| %.2f" % floor)

    tough = v.get("toughness", 0.0)
    for x in range(P0, P0 + 4):
        for y in range(17, 21):
            gt.call("/api/damage/apply", {"x": x + 0.5, "y": y + 0.5, "z": 8.5, "radius": 0.4,
                                          "energy": tough * 0.45, "collapse": False})
    time.sleep(2.0)
    c, m = voxel(P0, 17, 8).get("damage01", 0.0), voxel(P0 + 2, 19, 8).get("damage01", 0.0)
    if abs(c - m) > 0.02:
        print("RESULT %s UNTESTABLE non-uniform damage corner=%.3f centre=%.3f" % (label, c, m))
        return 3, None

    cam(P0 + 2.0, 18.5, 20.0)
    damaged = gt.capture()
    diff = sum(abs(damaged[i] - clean1[i]) for i in range(3))
    brighter = sum(damaged) > sum(clean1)
    print("  damaged (0.45) vs clean: |RGB| %.2f  damaged pane %s" %
          (diff, "BRIGHTER" if brighter else "DARKER"))
    ok = diff > 2.0 * max(floor, 0.5) and brighter
    print("RESULT %s %s diff=%.2f floor=%.2f frosted=%s damage01=%.3f"
          % (label, "PASS" if ok else "FAIL", diff, floor, brighter, c))
    return (0 if ok else 1), diff


def run_crack():
    setup_common()
    rc, _ = check_crack()
    restore_common()
    return rc


# ------------------------------------------------------------------------------------ 13.12 far
def run_far():
    """The crack seed is exact far from the origin.

    The same damaged-pane measurement near the origin and at x ~ 100 000 (chunk 3125). If the OIT
    shader seeds cracks from a float `inWorldPos + cameraWorld` sum, the far pane's crack reads
    differently or flickers; with the exact vChunkBaseAbs formula both agree.

    ORDER MATTERS (15.7): /api/world/generate for the far chunk resets the loaded world, which is
    what made the near pane "not build" on the first run. So NEAR is measured first, THEN the far
    chunk is generated and measured.
    """
    FAR = 100000
    print("CHECK far (13.12) - prediction: far diff within 25% of near, stable across captures")
    setup_common()
    rc_n, near = check_crack(label="crack-near")
    gt.call("/api/world/generate", {"type": "Flat", "from": {"x": FAR // 32, "y": 0, "z": 0},
                                    "to": {"x": FAR // 32 + 1, "y": 0, "z": 0}})
    if not wait_voxel(FAR + 8, 15, 8):
        print("RESULT far UNTESTABLE far-chunk-never-became-resident")
        restore_common()
        return 3
    rc_f, far = check_crack(base_x=FAR, label="crack-far")
    if near is None or far is None:
        print("RESULT far UNTESTABLE a pane did not build (near=%s far=%s)" % (near, far))
        restore_common()
        return 3
    cam(FAR + 8 + 2.0, 18.5, 20.0)
    repeats = [sum(gt.capture()) for _ in range(3)]
    spread = max(repeats) - min(repeats)
    restore_common()
    rel = abs(far - near) / max(near, 1e-6)
    ok = rel <= 0.25 and spread < 3.0
    print("RESULT far %s near=%.2f far=%.2f rel=%.2f capture-spread=%.2f"
          % ("PASS" if ok else "FAIL", near, far, rel, spread))
    return 0 if ok else 1


# ------------------------------------------------------------------------------------ 13.17 tint
def run_tint():
    """The pane shows GLASS's texture, not the placeholder.

    Run with a strongly GREEN glass texture temporarily swapped into resources/textures/source (the
    caller does the swap and the revert). Over a neutral Stone backdrop, a pane drawing glass's
    texture reads green-shifted; a pane drawing the placeholder does not. Prints the pane's green
    excess (G - (R+B)/2) against the backdrop's.
    """
    print("CHECK tint (13.17) - prediction: pane green-excess >> backdrop green-excess")
    setup_common()
    clear(4, GROUND_TOP, 3, 15, 26, 10)
    gt.fill(4, 15, 4, 15, 24, 4, "Stone")
    time.sleep(2.0)
    cam(9.5, 18.5, 20.0)
    back = gt.capture()
    gt.fill(8, 17, 8, 11, 20, 8, "Glass")
    time.sleep(3.0)
    cam(9.5, 18.5, 20.0)
    pane = gt.capture()
    restore_common()
    ge = lambda c: c[1] - (c[0] + c[2]) / 2.0
    ok = ge(pane) - ge(back) > 6.0
    print("RESULT tint %s pane_green_excess=%.2f backdrop_green_excess=%.2f"
          % ("PASS" if ok else "FAIL", ge(pane), ge(back)))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument("check", choices=["shadow", "crack", "far", "tint"])
    ap.add_argument("--port", type=int, default=8090)
    args = ap.parse_args()
    gt.BASE = "http://localhost:%d" % args.port
    return {"shadow": check_shadow, "crack": run_crack, "far": run_far, "tint": run_tint}[args.check]()


if __name__ == "__main__":
    sys.exit(main())
