"""F6.5 live (docs/evidence/voxfound_f6_prediction.txt V1-V5): rubble and poured water in the bodies lab.

Drives POST /api/debug/voxfound_bodies on a running editor: a hand-built FractureLab rig is broken (F4.1 + F5.1), its
crumbs become rubble in the lab's world (F6.3) stepped by WorldFluid (F6.2, the CPU reference) while the lab runs; then
1 m^3 of water is poured in front of the wall. Records the ledger, the fluid's rest tick and cost, the display check
(drawn vs the rule) and the region verify, with screenshots at one stated pose: before the run, at rest, after the pour.
  --display off : the red -- the fill steps but the preview is not edited (the check must count mismatches).
  --stress      : large_wall E/T 650 (no pour, no screenshots): the cost on the CPU reference.
"""
import argparse
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
import voxfound_f53_gate as g53  # noqa: E402
from voxfound_f53_gate import post, shot, save  # noqa: E402

EVID = os.path.join(os.path.dirname(__file__), "..", "docs", "evidence")
BODIES = "/api/debug/voxfound_bodies"
AT = [5.0, 20.0, 5.0]
CAM = {"position": {"x": 6.5, "y": 21.6, "z": 1.0}, "yaw": 90, "pitch": -14}   # F5.3's stated pose


def log(out, rec):
    with open(os.path.join(EVID, out), "a") as f:
        f.write(json.dumps(rec) + "\n")
    print(json.dumps(rec)[:1200], flush=True)


def status(**kw):
    return post(BODIES, dict({"status": True, "pen": False}, **kw))


def summary(s):
    f = s.get("fluid", {})
    return {"tick": s["tick"], "bodies": s["bodies"], "awake": s["awake"], "ledger": s["ledger"],
            "ledger_now_ok": s["exit"]["ledger_now_ok"], "ledger_checks": s["exit"]["ledger_checks"],
            "ledger_bad": s["exit"]["ledger_bad"], "fluid": {k: f.get(k) for k in (
                "ticks", "box_lo", "box_size", "grows", "quiet_tick", "rubble_units", "water_units", "display",
                "display_edits", "cells_changed", "ms_frames", "ms_median", "ms_p95", "ms_max",
                "check_mismatched_voxels")},
            "verify": s.get("verify"), "rest": {k: s["rest"].get(k) for k in ("passes", "baked", "voxels")}}


def wait_quiet(limit_s):
    t0 = time.time()
    while time.time() - t0 < limit_s:
        time.sleep(1.0)
        s = status()
        q = s.get("fluid", {}).get("quiet_tick", -1)
        if q is not None and q >= 0:
            return s, time.time() - t0
    return status(), -1.0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rig", default="stone_wall")
    ap.add_argument("--energy", type=float, default=80.0)
    ap.add_argument("--impulse", type=float, default=20.0)
    ap.add_argument("--display", choices=["on", "off"], default="on")
    ap.add_argument("--stress", action="store_true")
    ap.add_argument("--out", default="voxfound_f65_live.jsonl")
    ap.add_argument("--tag", default="")
    ap.add_argument("--world", default="CharacterTestbed")
    ap.add_argument("--cam", default=json.dumps(CAM), help="the stated pose (JSON for /api/camera)")
    a = ap.parse_args()
    if a.stress:
        a.rig, a.energy, a.impulse = "large_wall", 650.0, 200.0
    r = post(BODIES, {"rig": a.rig, "energy": a.energy, "impulse": a.impulse, "at": AT, "solver": "gpu",
                      "preview": "region", "rubble": True, "ledger_every": True})
    if "error" in r:
        print(r)
        sys.exit(1)
    post(BODIES, {"rest": {"on": True}, "fluid_display": a.display == "on"})
    base = {"gate": "F6.5 live", "cam": json.loads(a.cam), "world": a.world, "rig": a.rig, "energy": a.energy, "display": a.display,
            "tag": a.tag, "provenance": r.get("provenance"), "crumbs": r.get("crumbs"), "bodies0": r.get("bodies"),
            "grid": r.get("grid")}
    s0 = status(fluid_check=True)
    log(a.out, dict(base, phase="setup", **summary(s0)))
    stem = "voxfound_f65_%s%s" % (a.rig, ("_" + a.tag) if a.tag else "")
    if not a.stress:
        post("/api/camera", json.loads(a.cam))
        save(shot(1.0), stem + "_0_before.png")
    post(BODIES, {"run": True})
    s1, dt = wait_quiet(60.0 if a.stress else 20.0)
    s1 = status(fluid_check=not a.stress, verify=not a.stress)
    log(a.out, dict(base, phase="rubble rest", wall_s=dt, **summary(s1)))
    if a.stress:
        post(BODIES, {"run": False})
        return
    save(shot(0.5), stem + "_1_rubble_rest.png")
    # 1 m^3 of water in front of the wall: lab voxels x 40-47, y 2-9, z 20-27 (the rig sits at (32, 2, 32))
    post(BODIES, {"pour": {"lo": [40, 2, 20], "size": [8, 8, 8]}})
    s2, dt2 = wait_quiet(20.0)
    s2 = status(fluid_check=True, verify=True)
    log(a.out, dict(base, phase="pour rest", wall_s=dt2, **summary(s2)))
    save(shot(0.5), stem + "_2_pour_rest.png")
    # the cost once quiet: the run turned off and on (clears the cost ring), 5 s sampled
    post(BODIES, {"run": False})
    post(BODIES, {"run": True})
    time.sleep(5.0)
    log(a.out, dict(base, phase="quiet cost (5 s)", **summary(status())))
    post(BODIES, {"run": False})


if __name__ == "__main__":
    main()
