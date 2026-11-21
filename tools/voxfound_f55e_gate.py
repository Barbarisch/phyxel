#!/usr/bin/env python3
"""docs/VoxelFoundationPlan.md F5.5e -- the awake pile's tick, live (prediction: docs/evidence/voxfound_f55_prediction.txt,
F5.5e E1-E6; the headless half is VoxFoundBodySimGpu.F55eAwakePileProfile).

Drives POST /api/debug/voxfound_bodies on a running Release editor. Rigs are FractureLab's hand-built large wall (not
generator output) at E/T 250 / 650 / 2800, broken by F4.1 fractureReference, extracted by F5.1, moved by BodySimGpu.
Per rung: settle (rest off), then every body woken through BodySimGpu::edit ({"wake_all":true}) and
  a) 30 synchronous ticks, one per request (one per submit: F5.5d X2's measure);
  b) woken again, one request of 30 ticks (30 per submit);
  c) woken again, the per-pass profile on, 30 synchronous ticks, the status's profile block.
Writes JSON lines to docs/evidence/voxfound_f55e_live.jsonl.
F5.5g: --once on|off sets {"world_once"} with every wake (on = each world item tested once; off = the two-test pass).
F5.5h: --spread on|off sets {"world_spread"} with every wake (on = a big body's items over many workgroups).
F5.5j: --hard-spread on|off sets {"hard_spread"} with every wake (on = HARD_APPLY's items over many workgroups).
F5.5l: --pairs-once on|off sets {"pairs_once"} with every wake (on = each pair corner tested once).
F5.5k: --pairs-split adds {"pairs_split"} to the profile (p_broad / p_count / p_write / p_cap; "pairs" = NARROW alone).
F5.5m: --primal-split adds {"primal_split"} to the profile (q_floor / q_sum / q_red / q_solve: one iteration's copies).
"""
import argparse
import json
import os
import sys
import urllib.request

sys.path.insert(0, os.path.dirname(__file__))
import voxfound_f53_gate as g53  # noqa: E402
from voxfound_f53_gate import post  # noqa: E402

EVID = os.path.join(os.path.dirname(__file__), "..", "docs", "evidence")
BODIES = "/api/debug/voxfound_bodies"
AT = [5.0, 20.0, 5.0]
TAG = ""
OUT = "voxfound_f55e_live.jsonl"


def log(rec):
    rec = dict(rec, world=TAG)
    with open(os.path.join(EVID, OUT), "a") as f:
        f.write(json.dumps(rec) + "\n")
    print(json.dumps(rec)[:1500], flush=True)


def get(path):
    with urllib.request.urlopen(g53.API + path, timeout=120) as r:
        return json.loads(r.read())


def cmd(**kw):
    return post(BODIES, dict({"status": True, "pen": False}, **kw))


def once_kw(a):   # F5.5g; F5.5h
    kw = {"world_once": a.once == "on"} if a.once else {}
    if a.spread:
        kw["world_spread"] = a.spread == "on"
    if a.hard_spread:   # F5.5j
        kw["hard_spread"] = a.hard_spread == "on"
    if a.pairs_once:   # F5.5l
        kw["pairs_once"] = a.pairs_once == "on"
    return kw


def rung(a, e):
    r = post(BODIES, {"rig": "large_wall", "energy": e, "impulse": 200.0, "at": AT, "solver": "gpu",
                      "preview": "region", "gpu_edit": True})
    st = cmd(ticks=a.settle)
    settled = {"tick": st["tick"], "bodies": st["bodies"], "awake": st["awake"]}
    # a) one tick per request
    w = cmd(wake_all=True, **once_kw(a))
    per = []
    for _ in range(a.ticks):
        s = cmd(ticks=1)
        per.append({"awake": s["awake"], "ms": s["solver"]["ms_per_tick_device"],
                    "contacts": s["solver"]["stats"]["contacts"], "candidates": s["solver"]["stats"]["candidates"]})
    one = sorted(p["ms"] for p in per)
    # b) 30 ticks in one request (rest off: one submit)
    cmd(wake_all=True, **once_kw(a))
    s = cmd(ticks=a.ticks)
    thirty = s["solver"]["ms_per_tick_device"]
    awake30 = s["awake"]
    # c) the profile
    cmd(wake_all=True, profile=True, world_split=a.split, pairs_split=a.pairs_split, primal_split=a.primal_split,
        **once_kw(a))   # F5.5f; F5.5k; F5.5m
    for _ in range(a.ticks):
        s = cmd(ticks=1)
    prof = s["solver"].get("profile")
    cmd(profile=False, world_split=False, pairs_split=False, primal_split=False)
    t = get("/api/debug/engine_timing")
    gate = "F5.5m live primal split" if a.primal_split else "F5.5l live pairs once=" + a.pairs_once if a.pairs_once else "F5.5k live pairs split" if a.pairs_split else "F5.5j live hard spread=" + a.hard_spread if a.hard_spread else "F5.5h live world spread=" + a.spread if a.spread else "F5.5g live world once=" + a.once if a.once else ("F5.5f live world split" if a.split else "F5.5e live awake pile")
    log({"gate": gate, "energy": e, "rig": {k: r.get(k) for k in ("bodies", "crumbs") if k in r},
         "settled": settled, "awake_after_wake": w["awake"], "bodies": w["bodies"],
         "one_per_submit": {"median": one[len(one) // 2], "max": one[-1], "per_tick": per},
         "thirty_per_submit": thirty, "awake_after_30": awake30, "profile": prof,
         "stats": s["solver"]["stats"],
         "engine": {"gpu_frame_ms": t.get("gpu_frame_ms"), "cpu_frame_ms": t.get("cpuFrameTime"),
                    "present_mode": t.get("present_mode")}})


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--api", default="http://127.0.0.1:8090")   # "localhost" costs ~2 s per call here (F5.5c)
    ap.add_argument("--settle", type=int, default=600)
    ap.add_argument("--ticks", type=int, default=30)
    ap.add_argument("--energies", type=float, nargs="+", default=[250.0, 650.0, 2800.0])
    ap.add_argument("--split", action="store_true", help="F5.5f: the world split in the profile")
    ap.add_argument("--once", choices=["on", "off"], default=None, help="F5.5g: the world pass's items once / twice")
    ap.add_argument("--spread", choices=["on", "off"], default=None, help="F5.5h: a big body's items over many groups")
    ap.add_argument("--hard-spread", choices=["on", "off"], default=None,
                    help="F5.5j: HARD_APPLY's items over many groups")
    ap.add_argument("--pairs-once", choices=["on", "off"], default=None,
                    help="F5.5l: each pair corner tested once / twice")
    ap.add_argument("--pairs-split", action="store_true", help="F5.5k: the pairs group split in the profile")
    ap.add_argument("--primal-split", action="store_true", help="F5.5m: the primal split in the profile")
    ap.add_argument("--out", default="voxfound_f55e_live.jsonl")
    ap.add_argument("--world", default="", help="which project's world the lab runs in (recorded)")
    a = ap.parse_args()
    g53.API = a.api
    global TAG
    TAG = a.world
    global OUT
    OUT = a.out
    for e in a.energies:
        rung(a, e)
    return 0


if __name__ == "__main__":
    sys.exit(main())
