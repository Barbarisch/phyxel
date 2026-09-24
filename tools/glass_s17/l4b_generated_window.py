# -*- coding: utf-8 -*-
"""docs/GlassTransparency.md section 17.7 L4b -- a GENERATED glass window (the microcube path).

Built by the engine's structure generator (POST /api/structure/build, schema v2), never
hand-placed. Two builds at the SAME position, identical programs, one variable: the window
portal's infill, "glass" vs "open" (control). The glass leaf is a 1-micro pane in the reveal
(StructureRealizer.cpp:361-395); its edges touch the jambs, lintel and sill.

PREDICTIONS (written before running):
  A  opaque covered unit faces in the house box: glass == open, exactly. (The old rule hid the
     trim's contact strips along the leaf edges.)
  B  transparent covered unit faces with "open" = 0; with "glass" = the leaf's two broad faces
     only (edges lie on trim and stay culled), so an even number, printed.
"""
import json
import sys
import time

sys.path.insert(0, r"G:\Github\phyxel\tools")
import glass_transmission as gt   # noqa: E402

POS = {"x": 40, "y": 17, "z": 0}
BOX = (38, 16, -2, 48, 30, 8)          # world-cube box around the 5x5 house, roof included
CHUNKS = [(1, 0, 0), (1, 0, -1)]       # x 38-48 -> chunk x 1; z -2..-1 -> chunk z -1


def program(infill):
    return {
        "schema": "v2", "typology": "croft", "footprint": [5, 5], "position": POS,
        "name": "s17_l4b_" + infill, "function": "house",
        "stories": [{
            "height": 3,
            "rooms": [{"id": "hall", "rect": [0, 0, 5, 5], "purpose": "living"}],
            "portals": [
                {"between": ["exterior", "hall"], "pos": [0, 2], "width": 1, "height": 2, "kind": "door"},
                {"between": ["exterior", "hall"], "pos": [2, 0], "width": 1, "height": 1,
                 "kind": "window", "infill": infill},
            ],
        }],
    }


def settle():
    last, since = None, time.time()
    while True:
        now = tuple(gt.call("/api/debug/chunk_faces?cx=%d&cy=%d&cz=%d" % c).get("rebuilds") for c in CHUNKS)
        if now != last:
            last, since = now, time.time()
        elif time.time() - since > 3.0:
            return
        time.sleep(0.5)


def counts():
    op = tr = 0
    for c in CHUNKS:
        r = gt.call("/api/debug/chunk_faces?cx=%d&cy=%d&cz=%d&x1=%d&y1=%d&z1=%d&x2=%d&y2=%d&z2=%d"
                    % (c + BOX))
        if "error" in r:
            continue          # a chunk the house does not reach
        op += sum(r["opaque"].values())
        tr += sum(r["transparent"].values())
    return op, tr


def build(infill):
    r = gt.call("/api/structure/build", program(infill), timeout=300)
    brief = {k: r.get(k) for k in ("success", "placed", "failed", "typology", "error", "status")
             if k in r}
    print("[%s] build response: %s" % (infill, json.dumps(brief)))
    settle()
    return r


def shot(label):
    # Outside the window wall (z = 0 side), slightly off-axis so the reveal shows through the leaf.
    gt.call("/api/camera", {"position": {"x": 44.2, "y": 18.9, "z": -3.2}, "yaw": 112, "pitch": -4})
    time.sleep(2.0)
    p = gt.call("/api/screenshot").get("path")
    print("[%s] shot=%s" % (label, p))


def main():
    gt.call("/api/debug/tonemap", {"curve": 1, "exposure": 8.0})
    gt.job("clear_region", {"x1": BOX[0] - 4, "y1": 16, "z1": BOX[2] - 4,
                            "x2": BOX[3] + 4, "y2": BOX[4], "z2": BOX[5] + 4})
    gt.fill(BOX[0] - 4, 16, BOX[2] - 4, BOX[3] + 4, 16, BOX[5] + 4, "Stone")   # ground slab
    time.sleep(3.0)
    results = {}
    for infill in ("glass", "open"):
        gt.job("clear_region", {"x1": BOX[0], "y1": 17, "z1": BOX[2], "x2": BOX[3], "y2": BOX[4], "z2": BOX[5]})
        settle()
        build(infill)
        results[infill] = counts()
        print("[%s] opaque=%d transparent=%d" % ((infill,) + results[infill]))
        shot(infill)
    g, o = results["glass"], results["open"]
    a_ok = g[0] == o[0]
    b_ok = o[1] == 0 and g[1] > 0 and g[1] % 2 == 0
    print("A opaque glass == open: %s (%d vs %d, diff %d)" % ("PASS" if a_ok else "FAIL", g[0], o[0], g[0] - o[0]))
    print("B transparent open=0, glass=leaf faces only (%d): %s" % (g[1], "PASS" if b_ok else "FAIL"))
    print("RESULT", "PASS" if a_ok and b_ok else "FAIL")


if __name__ == "__main__":
    main()
