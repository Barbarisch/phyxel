"""WaterCore §46 (A13) - pins L5's distribution judge (tools/water_l5_judge.py). No engine.

    python tools/test_water_l5_judge.py

1. calibration: exchangeable data (far and near from one distribution) - the family's false-alarm rate stays at alpha;
2. power: a shift of one spread at 6 + 6 is caught; 3 + 3 says UNDERPOWERED, never PASS;
3. the spread test sees a narrower far scatter with the same centre (44.9's wood x);
4. order independence: shuffling the runs and the sessions changes nothing;
5. the red: the saved fade sessions (44.9, the old fence passed them pooled) FAIL on the stone's rest time;
6. sessions of different builds are not one pool."""
import json, math, pathlib, random, sys
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import numpy as np
import water_l5_judge as J

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_m4"
fails = []


def check(cond, msg):
    print(("ok   " if cond else "FAIL ") + msg)
    if not cond: fails.append(msg)


def family(rng, n_near, n_far, shift=0.0, scale=1.0, measures=4, sessions=1):
    s = {}
    for k in range(sessions):
        base = rng.normal(0, 3)   # sessions differ (another build, another day) - the stratification must absorb it
        near = [{f"m{i}": base + rng.normal() for i in range(measures)} for _ in range(n_near)]
        far = [{f"m{i}": base + shift + scale * rng.normal() for i in range(measures)} for _ in range(n_far)]
        s[f"s{k}"] = {"near": near, "far": far}
    return s


rng = np.random.default_rng(1)
# 1. calibration: 300 families of 8 tests (4 measures x location + spread), 6 + 6 in two sessions
N = 300
fa = sum(J.judge(family(rng, 6, 6, sessions=2), 0.05, 2000)["verdict"] == "FAIL" for _ in range(N))
bound = 0.05 + 3 * math.sqrt(0.05 * 0.95 / N)
check(fa / N <= bound, f"calibration: family false alarms {fa}/{N} = {fa / N:.3f} <= {bound:.3f}")

# 2. power and the underpowered verdict
hits = sum(J.judge(family(rng, 6, 6, shift=3.0, measures=1), 0.05, 4000)["verdict"] == "FAIL" for _ in range(50))
check(hits >= 45, f"power: a 3-sigma shift at 6 + 6 caught {hits}/50")
v = J.judge(family(rng, 3, 3, shift=10.0, measures=1), 0.05, 4000)
check(v["verdict"] != "PASS" and v["underpowered"], f"3 + 3 is never a PASS: {v['verdict']}, underpowered {v['underpowered']}")
v = J.judge(family(rng, 6, 6, measures=10), 0.05, 4000)
check(not v["underpowered"], f"6 + 6 is powered for 20 tests (strict {v['strict_threshold']:.5f}): underpowered {v['underpowered']}")
try:
    J.judge(family(rng, 6, 6), 0.05, 100); check(False, "perms 100 refused")
except ValueError: check(True, "perms 100 refused")

# 3. spread: the same centre, a tenth of the scatter
hits = sum(J.judge(family(rng, 8, 8, scale=0.1, measures=1), 0.05, 4000)["tests"]["m0:spread"]["rejected"] for _ in range(30))
check(hits >= 20, f"spread: a narrow far group at 8 + 8 caught {hits}/30")

# 4. order independence
s = family(rng, 6, 6, shift=0.5, sessions=3)
ref = J.judge(s, 0.05, 20000)
s2 = {k: {"near": random.Random(7).sample(v["near"], len(v["near"])), "far": random.Random(8).sample(v["far"], len(v["far"]))}
      for k, v in reversed(list(s.items()))}
got = J.judge(s2, 0.05, 20000)
check(all(abs(ref["tests"][k]["p"] - got["tests"][k]["p"]) < 1e-12 for k in ref["tests"]) and ref["verdict"] == got["verdict"],
      "order: runs and sessions shuffled - the same p for every test")

# 5. the red: 44.9's fade sessions, judged alone (one build)
import water_l5_outcome as L5
runs = []
for t in ("fade_on", "fade_on_interleaved"):
    runs += [{**r, "run": f"{r['run']}@{t}"} for r in json.loads((EV / f"l5_{t}.json").read_text(encoding="utf-8"))["runs"]]
v = L5.verdict(runs)
d = v["distribution"]
check(d["verdict"] == "FAIL" and "stone_rest_t:location" in d["rejected"],
      f"red: the fade FAILS on the stone's rest time (rejected {d['rejected']})")

# 6. pool keys: a recorded build splits pools; an unrecorded one is None
a = {"identity": {"exe_sha256": "aa"}, "displace": True, "handover": {"mm": 1.5}}
check(L5.pool_key(a) != L5.pool_key({**a, "identity": {"exe_sha256": "bb"}}), "pool: two binaries are two treatments")
check(L5.pool_key(a) != L5.pool_key({**a, "displace": False}), "pool: two settings are two treatments")
check(L5.pool_key({"displace": True}) is None, "pool: a pre-A13 session has no key")

print(f"\n{len(fails)} failed" if fails else "\nall passed")
sys.exit(1 if fails else 0)
