#!/usr/bin/env python3
"""docs/VoxelFoundationPlan.md F5.5d -- the F5 exit, live (prediction: docs/evidence/voxfound_f55_prediction.txt, F5.5d
X1-X8).

Drives POST /api/debug/voxfound_bodies on a running Release editor. Rigs are FractureLab's hand-built large wall (not
generator output) at E/T 250 / 650 / 2800 (the 100 / 300 / 1,000 rungs), broken by F4.1 fractureReference, extracted
by F5.1, moved by BodySimGpu (the shipped path), baked by the F5.4 policy with F5.5c's levers.
  ladder -- per rung: X1/X3/X4/X5/X8 a 60 s run with the rest policy on (budget 64, keep 0, rest 60 ticks, cap 4) and
            the ledger checked after every rest call; the check's red (a planted wrong reference must count as bad);
            the body passes after the backlog; then X2 (settle, wake every body, 30 synchronous ticks) and, at the
            1,000 rung, X6 (draw at the peak vs the same voxels as world) and X7 (pop near / far across one bake_now).
            Then the X8 control: the same pose with the rig cleared, sampled the same way.
  seq    -- the owner's eye check: the far pose during a capped run, a frame every 0.5 s (not gated).
Writes JSON lines to docs/evidence/voxfound_f55d_live.jsonl and PNGs to docs/evidence/voxfound_f55d_*.png.
"""
import argparse
import json
import math
import os
import sys
import time
import urllib.request

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
import voxfound_f53_gate as g53  # noqa: E402
from voxfound_f53_gate import post, shot, save  # noqa: E402

EVID = os.path.join(os.path.dirname(__file__), "..", "docs", "evidence")
BODIES = "/api/debug/voxfound_bodies"
AT = [5.0, 20.0, 5.0]
WHOLE = {"position": {"x": 21.0, "y": 28.0, "z": -25.0}, "yaw": 90, "pitch": -20}
REST = {"on": True, "budget": 64, "keep_m": 0.0, "rest_ticks": 60, "cap": 4}
BODY_KEYS = ["Scene Pass/VoxFound Bodies", "Shadow Pass/Shadow Mid/VoxFound Bodies Shadow",
             "Shadow Pass/Shadow Near/VoxFound Bodies Shadow"]
TAG = ""


def log(rec):
    rec = dict(rec, world=TAG)
    with open(os.path.join(EVID, "voxfound_f55d_live.jsonl"), "a") as f:
        f.write(json.dumps(rec) + "\n")
    print(json.dumps(rec)[:2000], flush=True)


def get(path):
    with urllib.request.urlopen(g53.API + path, timeout=120) as r:
        return json.loads(r.read())


def status(**kw):
    return post(BODIES, dict({"status": True, "pen": False}, **kw))


def rig(energy, **kw):
    return post(BODIES, dict({"rig": "large_wall", "energy": energy, "impulse": 200.0, "at": AT, "solver": "gpu",
                              "preview": "region", "gpu_edit": True}, **kw))


def timing():
    t = get("/api/debug/engine_timing")
    return {"cpu_frame_ms": t.get("cpuFrameTime"), "frame_ms_max": t.get("frame_ms_max_since_last_read"),
            "gpu_frame_ms": t.get("gpu_frame_ms")}


def body_passes(frames=120):
    t = get(f"/api/debug/gpu_timing?frames={frames}")
    # gpu_timing is a HISTORY of the last `frames` frames, and a scope absent from a frame is not a 0 in its median:
    # the caller must wait out the window after a mode change (run 1 of X6 slept 1.5 s -- its pairs mixed modes);
    # n per scope says how many of the frames drew it
    med = {s["key"]: s["median_ms"] for s in t["scopes"]}
    n = {s["key"]: s["n"] for s in t["scopes"]}
    return {"main": med.get(BODY_KEYS[0], 0.0), "mid": med.get(BODY_KEYS[1], 0.0), "near": med.get(BODY_KEYS[2], 0.0),
            "frame": t["gpu_frame_ms"]["median_ms"], "n": [n.get(k, 0) for k in BODY_KEYS],
            "frames_used": t.get("frames_used")}


def sample_timing(seconds):
    """X8: engine_timing every 0.5 s (the same measure for the run and the control)."""
    out = []
    t0 = time.time()
    while time.time() - t0 < seconds:
        time.sleep(0.5)
        out.append(timing())
    return out


def tsum(ts):
    cpu = sorted(x["cpu_frame_ms"] for x in ts if x["cpu_frame_ms"] is not None)
    mx = sorted(x["frame_ms_max"] for x in ts if x["frame_ms_max"] is not None)
    q = lambda v, f: v[min(len(v) - 1, int(f * len(v)))] if v else None  # noqa: E731
    return {"n": len(ts), "cpu_frame_median_ms": q(cpu, 0.5), "max_per_window_median_ms": q(mx, 0.5),
            "max_per_window_p90_ms": q(mx, 0.9), "max_ms": mx[-1] if mx else None,
            "max_ms_without_largest": mx[-2] if len(mx) > 1 else None}


def run_rung(a, e):
    r = rig(e, ledger_every=True)
    post("/api/camera", WHOLE)
    post(BODIES, {"rest": REST})
    st0 = status()
    ledger0 = st0["ledger"]
    floor_y = AT[1] - 1.0
    post(BODIES, {"run": True})
    t0 = time.time()
    samples, prev = [], None
    while time.time() - t0 < a.seconds:
        time.sleep(0.5)
        st = status()
        now = time.time()
        s = {"t": round(now - t0, 2), "tick": st["tick"], "bodies": st["bodies"], "awake": st["awake"],
             "below_floor": sum(1 for c in st["centres_m"] if c[1] < floor_y), "ledger_ok": st["ledger"] == ledger0,
             "baked": st["rest"]["baked"], "overwritten": st["rest"]["overwritten"], "refused": st["rest"]["refused"],
             "ledger_checks": st["exit"]["ledger_checks"], "ledger_bad": st["exit"]["ledger_bad"],
             "timing": timing()}
        if prev:
            s["ticks_per_s"] = (s["tick"] - prev[0]) / (now - prev[1])
        prev = (s["tick"], now)
        samples.append(s)
    post(BODIES, {"run": False})
    st = status(verify=True)
    time.sleep(a.window_wait)   # the 120-frame history after the run (a stopped run draws the same bodies)
    after = body_passes(120)
    # the check's red: a planted wrong reference must count as bad, and removing it must count as good again
    bad0 = st["exit"]["ledger_bad"]
    post(BODIES, {"ledger_plant": 1})
    s1 = post(BODIES, {"bake_now": {"budget": 1000000}, "status": True, "pen": False})
    post(BODIES, {"ledger_plant": -1})
    s2 = post(BODIES, {"bake_now": {"budget": 1000000}, "status": True, "pen": False})
    red = {"bad_before": bad0, "bad_with_plant": s1["exit"]["ledger_bad"], "now_ok_with_plant": s1["exit"]["ledger_now_ok"],
           "bad_after_unplant": s2["exit"]["ledger_bad"], "now_ok_after_unplant": s2["exit"]["ledger_now_ok"]}
    log({"gate": "X1/X3/X4/X5/X8 run", "energy": e, "rig": r, "rest": REST, "seconds": a.seconds,
         "ledger_broken_samples": sum(not s["ledger_ok"] for s in samples),
         "below_floor_max": max((s["below_floor"] for s in samples), default=0), "samples": samples,
         "timing_summary": tsum([s["timing"] for s in samples]),
         "final": {"bodies": st["bodies"], "awake": st["awake"], "rest": st["rest"], "levers": st["bake_levers"],
                   "frames": st["frames"], "solver": st["solver"], "verify": st.get("verify"), "exit": st["exit"],
                   "render": st["render"], "ledger_ok": st["ledger"] == ledger0},
         "body_passes_after_backlog": after, "ledger_check_red": red})


def wake_rung(a, e):
    r = rig(e)
    st = post(BODIES, {"ticks": a.settle, "status": True, "pen": False})
    settled = {"tick": st["tick"], "bodies": st["bodies"], "awake": st["awake"]}
    w = post(BODIES, {"wake_all": True, "status": True, "pen": False})
    per = []
    for k in range(a.wake_ticks):
        s = post(BODIES, {"ticks": 1, "status": True, "pen": False})
        per.append({"tick": s["tick"], "awake": s["awake"], "ms": s["solver"]["ms_per_tick_device"],
                    "contacts": s["solver"].get("stats", {}).get("contacts"),
                    "colours": s["solver"].get("stats", {}).get("colours")})
    ms = sorted(p["ms"] for p in per)
    log({"gate": "X2 every body awake", "energy": e, "rig": r, "settled": settled, "awake_after_wake": w["awake"],
         "bodies": w["bodies"], "per_tick": per, "ms_median": ms[len(ms) // 2], "ms_max": ms[-1]})
    return r


def px1(a, b, box):
    x0, y0, x1, y1 = box
    d = np.abs(a[y0:y1, x0:x1].astype(int) - b[y0:y1, x0:x1].astype(int)).max(axis=2)
    return {"px_gt1": int((d > 1).sum()), "px_box": int(d.size), "pct_gt1": 100.0 * float((d > 1).mean())}


def pose_on_ray(base, dist):
    """The camera `dist` metres back along base's view ray from the point 6 m in front of base."""
    y, p = math.radians(base["yaw"]), math.radians(base["pitch"])
    d = (math.cos(p) * math.cos(y), math.sin(p), math.cos(p) * math.sin(y))
    b = base["position"]
    tgt = (b["x"] + 6.0 * d[0], b["y"] + 6.0 * d[1], b["z"] + 6.0 * d[2])
    return {"position": {"x": tgt[0] - dist * d[0], "y": tgt[1] - dist * d[1], "z": tgt[2] - dist * d[2]},
            "yaw": base["yaw"], "pitch": base["pitch"]}


def draw_and_pop(a):
    post("/api/debug/tonemap", {"curve": 0})
    post("/api/debug/effect_time", {"frozen": True})
    r = rig(2800.0)
    st = post(BODIES, {"ticks": a.settle, "status": True, "pen": False})
    log({"gate": "X6/X7 setup", "rig": r, "tick": st["tick"], "bodies": st["bodies"], "awake": st["awake"]})
    # X6: the body passes vs the same voxels as world, 3 alternating pairs of 120 frames, at the whole pose
    post("/api/camera", WHOLE)
    time.sleep(a.cam_settle)
    ab = {"bodies": [], "as_world": []}
    for k in range(3):
        for mode in ("bodies", "as_world"):
            post(BODIES, {"as_world": mode == "as_world"})
            time.sleep(a.window_wait)   # past the 120-frame history (run 1: 1.5 s, mixed)
            ab[mode].append(body_passes(120))
    post(BODIES, {"as_world": False})
    st = status()
    log({"gate": "X6 draw at the peak", "bodies": st["bodies"], "awake": st["awake"], "render": st["render"],
         "ab": ab})
    # X7: pop across one bake_now at near (6 m) and far (24 m on the same ray); A/A control at each
    near = {"position": {"x": 15.0, "y": 22.5, "z": 0.0}, "yaw": 90, "pitch": -20}
    far = pose_on_ray(near, a.far)
    before = {}
    for name, cam in (("near", near), ("far", far)):
        post("/api/camera", cam)
        time.sleep(a.cam_settle)
        before[name] = (shot(1.0), shot(0.6))
    l0 = status()["ledger"]
    b = post(BODIES, {"bake_now": {"budget": 0, "keep_m": 0.0, "rest_ticks": 0, "cap": 0}, "status": True,
                      "pen": False})
    rec = {"gate": "X7 pop", "far_m": a.far, "bodies_before": st["bodies"], "bodies_after": b["bodies"],
           "baked": b["rest"]["baked"], "refused": b["rest"]["refused"], "ledger_equal": b["ledger"] == l0,
           "near_cam": near, "far_cam": far, "box": a.box}
    for name, cam in (("near", near), ("far", far)):
        post("/api/camera", cam)
        time.sleep(a.cam_settle)
        aft = shot(1.0)
        a1, a2 = before[name]
        rec[name] = {"control": px1(a1, a2, a.box), "pop": px1(a2, aft, a.box)}
        save(a2, f"voxfound_f55d_pop_{name}_bodies.png")
        save(aft, f"voxfound_f55d_pop_{name}_baked.png")
    log(rec)
    # X6's control on the same pixels: as_world writes the bodies back where they were BEFORE the break (the intact
    # wall), not where they lie -- so the pile baked into the world (just above) at the same whole pose is the control.
    # Not alternating (a bake cannot be undone): 3 windows of 120 frames after vs the 3 "bodies" windows before.
    post("/api/camera", WHOLE)
    time.sleep(max(a.cam_settle, a.window_wait))
    after = []
    for _ in range(3):
        after.append(body_passes(120))
        time.sleep(a.window_wait)
    st = status()
    log({"gate": "X6 control: the pile baked into the world", "bodies": st["bodies"], "after_bake": after,
         "before_bake_bodies": ab["bodies"]})
    post("/api/debug/effect_time", {"frozen": False})


def control(a):
    post(BODIES, {"clear": True})
    post("/api/camera", WHOLE)
    time.sleep(2.0)
    ts = sample_timing(a.seconds)
    log({"gate": "X8 control (rig cleared)", "seconds": a.seconds, "timing_summary": tsum(ts), "samples": ts})


def ladder(a):
    post("/api/debug/effect_time", {"frozen": False})
    for e in a.energies:
        run_rung(a, e)
    for e in a.energies:
        wake_rung(a, e)
    draw_and_pop(a)
    control(a)


def seq(a):
    post("/api/debug/effect_time", {"frozen": False})
    near = {"position": {"x": 15.0, "y": 22.5, "z": 0.0}, "yaw": 90, "pitch": -20}
    far = pose_on_ray(near, 24.0)
    rig(2800.0)
    post("/api/camera", far)
    post(BODIES, {"rest": REST})
    post(BODIES, {"run": True})
    time.sleep(3.0)
    frames = []
    for k in range(12):
        img = shot(0.5)
        st = status()
        save(img, f"voxfound_f55d_seq_far_{k:02d}.png")
        frames.append({"k": k, "tick": st["tick"], "bodies": st["bodies"], "baked": st["rest"]["baked"]})
    post(BODIES, {"run": False})
    log({"gate": "X7 sequence (owner's eye check, not gated)", "cam": far, "frames": frames})


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("what", choices=["ladder", "seq", "rung", "wake", "pop", "control"])
    ap.add_argument("--api", default="http://127.0.0.1:8090")   # "localhost" costs ~2 s per call here (F5.5c)
    ap.add_argument("--seconds", type=float, default=60.0)
    ap.add_argument("--settle", type=int, default=600)
    ap.add_argument("--wake-ticks", type=int, default=30)
    ap.add_argument("--energies", type=float, nargs="+", default=[250.0, 650.0, 2800.0])
    ap.add_argument("--box", type=int, nargs=4, default=[241, 44, 1198, 672], help="viewport x0 y0 x1 y1 (px)")
    ap.add_argument("--cam-settle", type=float, default=5.0)
    # X7's far distance on the near ray (24 = the prediction's 4x; run 1 showed a 32 m wall still fills the frame
    # there, so a follow-up measured at 96, stated as a follow-up)
    ap.add_argument("--far", type=float, default=24.0)
    ap.add_argument("--window-wait", type=float, default=4.0, help="s to wait out gpu_timing's 120-frame history")
    ap.add_argument("--world", default="", help="which project's world the lab runs in (recorded)")
    a = ap.parse_args()
    g53.API = a.api
    global TAG
    TAG = a.world
    if a.what == "rung":
        for e in a.energies:
            run_rung(a, e)
    elif a.what == "wake":
        for e in a.energies:
            wake_rung(a, e)
    elif a.what == "pop":
        draw_and_pop(a)
    elif a.what == "control":
        control(a)
    else:
        {"ladder": ladder, "seq": seq}[a.what](a)
    return 0


if __name__ == "__main__":
    sys.exit(main())
