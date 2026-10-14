"""A5 item 5 — grounding cost at scale (docs/AnimationSystemV3Plan.md §4 A5).

Spawns N walking NPCs on a clear patch, samples the engine frame time with the terrain foot solve
OFF and ON for every NPC (same NPCs, same walk), and prints the per-character cost. Run on a
RELEASE engine (Debug timings lie — memory: Character Pipeline Scaling) with CharacterTestbed:

    python tools/interaction_pipeline/grounding_cost.py --n 100 --seconds 8

The knob is per character (`POST /api/debug/foot_ik {id, enabled}`); the readback is
`GET /api/debug/engine_timing`. Nothing here is a pass/fail — it is the number the default flip
is decided on.
"""
from __future__ import annotations

import argparse
import json
import statistics
import time
import urllib.request

BASE = "http://127.0.0.1:8090"
SPOT = (100, 17, 100)


def post(path, body, timeout=30):
    req = urllib.request.Request(BASE + path, json.dumps(body).encode(), {"Content-Type": "application/json"})
    try:
        return json.load(urllib.request.urlopen(req, timeout=timeout))
    except Exception as e:  # noqa: BLE001
        return {"error": str(e)}


def get(path, timeout=30):
    try:
        return json.load(urllib.request.urlopen(BASE + path, timeout=timeout))
    except Exception as e:  # noqa: BLE001
        return {"error": str(e)}


def frame_ms_samples(seconds: float, period: float = 0.25):
    """Sample the engine's frame time for `seconds`; returns the list of ms values it reported."""
    out = []
    t0 = time.time()
    while time.time() - t0 < seconds:
        t = get("/api/debug/engine_timing")
        ms = None
        # Application::setEngineTimingHandler: cpuFrameTime (ms), gpuFrameTime, fps, and a
        # per-stage block with totalFrameTime / physicsTime / instanceUpdateTime.
        if isinstance(t, dict):
            for key in ("cpuFrameTime", "frame_ms", "frameMs"):
                if key in t and t[key] is not None:
                    ms = float(t[key]); break
            if ms is None:
                for v in t.values():
                    if isinstance(v, dict) and "totalFrameTime" in v:
                        ms = float(v["totalFrameTime"]); break
        if ms is not None:
            out.append(ms)
        time.sleep(period)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, default=100)
    ap.add_argument("--seconds", type=float, default=8.0)
    ap.add_argument("--keep", action="store_true", help="leave the NPCs in the world")
    args = ap.parse_args()

    ids = []
    side = max(1, int(args.n ** 0.5))
    for i in range(args.n):
        name = f"GC_{i:03d}"
        x = SPOT[0] + (i % side) * 2.0
        z = SPOT[2] + (i // side) * 2.0
        r = post("/api/npc/spawn", {"name": name, "position": {"x": x, "y": SPOT[1], "z": z},
                                    "appearance": {"preset": "standard"}})
        if r.get("success"):
            ids.append(f"npc_{name}")
    print(f"spawned {len(ids)} / {args.n}")
    # walk: wander behaviour so every character is in a locomotion state
    for eid in ids:
        post("/api/npc/behavior", {"name": eid.replace("npc_", ""), "behavior": "wander"})
    time.sleep(2.0)

    def set_all(enabled: bool):
        ok = 0
        for eid in ids:
            r = post("/api/debug/foot_ik", {"id": eid, "enabled": enabled})
            ok += 1 if r.get("success") and r.get("enabled") == enabled else 0
        return ok

    print(f"foot IK OFF on {set_all(False)} characters"); time.sleep(1.0)
    off = frame_ms_samples(args.seconds)
    print(f"foot IK ON  on {set_all(True)} characters"); time.sleep(1.0)
    on = frame_ms_samples(args.seconds)
    print(f"foot IK OFF on {set_all(False)} characters (A-B-A)"); time.sleep(1.0)
    off2 = frame_ms_samples(args.seconds)

    def summ(v):
        return (statistics.median(v), statistics.mean(v), max(v)) if v else (float("nan"),) * 3

    for label, v in (("OFF", off), ("ON ", on), ("OFF(2)", off2)):
        med, mean, mx = summ(v)
        print(f"frame ms {label}: median {med:.2f}  mean {mean:.2f}  max {mx:.2f}  (n={len(v)})")
    if off and on and ids:
        d = statistics.median(on) - statistics.median(off + off2)
        print(f"delta ON-OFF: {d:+.3f} ms/frame for {len(ids)} characters = {1000.0 * d / len(ids):+.1f} us/char/frame")

    if not args.keep:
        for eid in ids:
            post("/api/npc/remove", {"name": eid.replace("npc_", "")})


if __name__ == "__main__":
    main()
