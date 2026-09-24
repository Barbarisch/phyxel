# -*- coding: utf-8 -*-
"""docs/GlassTransparency.md section 17.7 -- L4 live check: opaque faces behind glass are drawn.

Scene (DamageLab, chunk y band 0):
  D  window 3x4 glass cubes (x 27-29, y 19-22, z 8) in a 1-thick stone wall (x 25-31, y 17-24, z 8)
     -- wholly inside chunk (0,0,0)
  E  the same window shape straddling the x=31|32 chunk border: wall x 29-35, y 17-24, z 20;
     window x 31-33, y 19-22 (window cells in BOTH chunks, reveal cells in both)

For each window, arms at the SAME camera pose and the same binary:
  glass  -> air (control: air never hid a face, so the reveal is drawn) -> glass (repeatability)

PREDICTIONS (written before running, from the scene + a live scan):
  A  opaque covered unit faces in the window box: glass == air, exactly. The old code would have
     lost P cube faces (P printed from a live neighbour scan, expected 14 per window).
  B  transparent covered unit faces in the box = the window's front+back only:
     3 x 4 x 2 = 24 cube faces = 1944 unit faces. Its edges lie on stone and stay culled.
"""
import math
import sys
import time

sys.path.insert(0, r"G:\Github\phyxel\tools")
import glass_transmission as gt   # noqa: E402

WINDOWS = {
    "D": dict(wall=(25, 17, 8, 31, 24, 8), win=(27, 19, 8, 29, 22, 8), chunks=[(0, 0, 0)]),
    "E": dict(wall=(29, 17, 20, 35, 24, 20), win=(31, 19, 20, 33, 22, 20), chunks=[(0, 0, 0), (1, 0, 0)]),
}
DIRS = [(1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1)]


def voxel(x, y, z):
    return gt.call("/api/world/voxel?x=%d&y=%d&z=%d" % (x, y, z))


def wait_mat(x, y, z, mat, timeout=60):
    t0 = time.time()
    while time.time() - t0 < timeout:
        v = voxel(x, y, z)
        if (mat is None and not v.get("exists")) or (mat and v.get("material") == mat):
            return True
        time.sleep(0.4)
    sys.exit("TIMEOUT waiting for %s at (%d,%d,%d)" % (mat, x, y, z))


def cells(box):
    x0, y0, z0, x1, y1, z1 = box
    for x in range(x0, x1 + 1):
        for y in range(y0, y1 + 1):
            for z in range(z0, z1 + 1):
                yield x, y, z


def scan_p(win):
    """P = opaque neighbour cells' faces pointing INTO a window cell, from the live world."""
    wset = set(cells(win))
    p = 0
    for (x, y, z) in wset:
        for d in DIRS:
            n = (x + d[0], y + d[1], z + d[2])
            if n in wset:
                continue
            v = voxel(*n)
            if v.get("exists") and v.get("material") != "Glass":
                p += 1
    return p


def settle(chunks):
    """Wait until every chunk's rebuild counter is stable for 2 s (all re-meshes, incl. ripples, landed)."""
    last, stable_since = None, time.time()
    while True:
        now = tuple(face_counts(c, None)["rebuilds"] for c in chunks)
        if now != last:
            last, stable_since = now, time.time()
        elif time.time() - stable_since > 2.0:
            return now
        time.sleep(0.3)


def face_counts(chunk, box):
    q = "/api/debug/chunk_faces?cx=%d&cy=%d&cz=%d" % chunk
    if box:
        q += "&x1=%d&y1=%d&z1=%d&x2=%d&y2=%d&z2=%d" % box
    r = gt.call(q)
    if "error" in r or "_http_status" in r:
        sys.exit("chunk_faces failed: %s" % r)
    return r


def box_totals(w):
    op = tr = quads = 0
    for c in w["chunks"]:
        r = face_counts(c, w["wall"])
        op += sum(r["opaque"].values())
        tr += sum(r["transparent"].values())
        quads += r["quads"]
    return op, tr, quads


def set_window(w, mat):
    x0, y0, z0, x1, y1, z1 = w["win"]
    if mat is None:
        gt.job("clear_region", {"x1": x0, "y1": y0, "z1": z0, "x2": x1, "y2": y1, "z2": z1})
        wait_mat(x1, y1, z1, None)
        wait_mat(x0, y0, z0, None)
    else:
        gt.fill(x0, y0, z0, x1, y1, z1, mat)
        wait_mat(x1, y1, z1, mat)
        wait_mat(x0, y0, z0, mat)


def camera_for(w, tag):
    x0, y0, z0, x1, y1, z1 = w["win"]
    cx, cy, cz = (x0 + x1 + 1) / 2.0, (y0 + y1 + 1) / 2.0, z0 + 0.5
    # Grazing view from the front-right: the reveal's left jamb faces are seen THROUGH the pane.
    px, py, pz = cx + 4.5, cy + 0.3, cz + 5.5
    yaw = math.degrees(math.atan2(cz - pz, cx - px))   # editor free-cam: yaw 0 = +X, -90 = -Z
    gt.call("/api/camera", {"position": {"x": px, "y": py, "z": pz}, "yaw": yaw, "pitch": -2})
    time.sleep(2.0)
    return gt.call("/api/screenshot").get("path")


def main():
    gt.call("/api/debug/tonemap", {"curve": 1, "exposure": 8.0})   # shipped look
    gt.call("/api/debug/shadow", {"mode": 0})
    gt.job("clear_region", {"x1": 0, "y1": 17, "z1": 0, "x2": 40, "y2": 30, "z2": 24})
    gt.fill(0, 17, 3, 23, 20, 3, "Bricks")
    for w in WINDOWS.values():
        x0, y0, z0, x1, y1, z1 = w["win"]
        gt.fill(x0, y0, z0, x1, y1, z1, "Glass")   # window first: fill never overwrites
        wait_mat(x1, y1, z1, "Glass")
        a = w["wall"]
        gt.fill(*a, "Stone")
        wait_mat(a[3], a[4], a[5], "Stone")
        wait_mat(a[0], a[1], a[2], "Stone")
    for x0 in (2, 10, 18):
        gt.fill(x0, 17, 8, x0 + 4, 23, 8, "Glass")
    wait_mat(22, 23, 8, "Glass")

    ok = True
    for name, w in WINDOWS.items():
        p = scan_p(w["win"])
        print("[%s] live scan: P = %d opaque faces point into the window (predicted 14)" % (name, p))
        arms = []
        for label, mat in (("glass", "Glass"), ("air", None), ("glass again", "Glass")):
            if label != "glass":
                set_window(w, mat)
            settle(w["chunks"])
            op, tr, quads = box_totals(w)
            shot = camera_for(w, label)
            arms.append((label, op, tr, quads, shot))
            print("[%s] %-11s opaque=%6d (%.2f cube faces)  transparent=%5d (%.2f)  quads=%4d  shot=%s"
                  % (name, label, op, op / 81.0, tr, tr / 81.0, quads, shot))
        g, a_, g2 = arms
        a_ok = g[1] == a_[1] == g2[1]
        b_ok = g[2] == g2[2] == 24 * 81 and a_[2] == 0
        print("[%s] A opaque glass == air == glass-again: %s   (old code: glass arm short by P*81 = %d)"
              % (name, "PASS" if a_ok else "FAIL", p * 81))
        print("[%s] B transparent == 1944 in both glass arms, 0 with air: %s" % (name, "PASS" if b_ok else "FAIL"))
        ok = ok and a_ok and b_ok and p == 14
    print("RESULT", "PASS" if ok else "FAIL")


if __name__ == "__main__":
    main()
