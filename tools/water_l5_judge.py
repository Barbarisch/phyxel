"""WaterCore §46 (A13) - L5's distribution judge: do the far runs come from the same outcomes as the near runs?

L5's first judge (water_l5_outcome.verdict) is a fence: each far value must lie inside the near runs' min-max band, widened by the
spread. It catches one wild run (far_dry) but not a far GROUP that differs systematically: 44.9's distance fade passed it 7 of 7
while every far stone settled ~2 s sooner (2.8-3.3 s against 4.0-5.8 s), because pooling sessions widened the band.

This judge compares the groups. For every measure, two stratified permutation tests of far against near:
  location   each run's value, ranked inside its own session (midranks / (n + 1)), summed over the far runs;
  spread     each run's distance from its session's median, ranked the same way (a far group with the same centre but a
             narrower or wider scatter - 44.9's wood x - shows here, not in location).
Labels are shuffled only INSIDE a session (the stratum), so sessions on different builds or days are never compared with each other,
only pooled. Two-sided p, exact when the shufflings are few, else Monte Carlo with a fixed seed (the same files give the same
verdict). Holm over all the tests at the family rate alpha.

Verdict: FAIL when any test is rejected; PASS when none is and every test had the power to reject (its best achievable p, with no
ties, beats Holm's strictest threshold alpha / m); otherwise UNDERPOWERED - never a pass. With 20 tests at alpha 0.05 one session
needs 6 near + 6 far (complete separation: p = 2 / C(12, 6) = 0.0022 < 0.0025).

A rest time of None (never rested) ranks above every time. Pure functions, numpy only; tools/test_water_l5_judge.py pins them."""
import itertools, math
import numpy as np

NEVER = 1e9   # a rest time of None: never rested - longer than any time


def midranks(v):
    """1-based midranks of v (ties share the mean of their ranks)."""
    v = np.asarray(v, dtype=float)
    order = np.argsort(v, kind="stable")
    r = np.empty(len(v))
    s = v[order]
    i = 0
    while i < len(v):
        j = i
        while j + 1 < len(v) and s[j + 1] == s[i]: j += 1
        r[order[i:j + 1]] = (i + j) / 2.0 + 1.0
        i = j + 1
    return r


def _scores(vals, kind):
    """A stratum's values -> each run's score in (0, 1): its normalized midrank (location), or the normalized midrank of its distance
    from the stratum's median (spread)."""
    v = np.asarray(vals, dtype=float)
    if kind == "spread": v = np.abs(v - np.median(v))
    return midranks(v) / (len(v) + 1.0)


def _best_p(strata):
    """The smallest two-sided p the design can give (no ties): every stratum completely separated, either way."""
    p = 1.0
    for n, f in strata: p /= math.comb(n, f)
    return min(1.0, 2.0 * p)


def perm_test(groups, kind, perms=20000, seed=46):
    """groups: [(near_values, far_values)] one per stratum (session). Returns {p, stat, expected, method, best_p, strata}."""
    groups = [(list(a), list(b)) for a, b in groups if len(a) and len(b)]
    if not groups: return {"p": None, "method": "no stratum holds both near and far runs", "best_p": None, "strata": []}
    per, obs, exp, total = [], 0.0, 0.0, 1
    for near, far in groups:
        sc = _scores(near + far, kind)
        n, f = len(sc), len(far)
        obs += sc[len(near):].sum(); exp += f * sc.mean()
        per.append((np.sort(sc), f)); total *= math.comb(n, f)
    # the shuffled distribution depends only on each stratum's multiset of scores: draw from a canonical order (sorted scores, sorted
    # strata), so the same evidence in another run or session order gives the same Monte Carlo p, not only the same exact one
    per.sort(key=lambda e: (len(e[0]), e[1], tuple(e[0])))
    if total <= perms:   # exact: every assignment of the far labels inside each stratum
        dist = np.zeros(1)
        for sc, f in per:
            t = np.array([sc[list(c)].sum() for c in itertools.combinations(range(len(sc)), f)])
            dist = np.add.outer(dist, t).ravel()
        dev = np.abs(dist - exp)
        p = float(np.mean(dev >= abs(obs - exp) - 1e-12))
        method = f"exact ({total} assignments)"
    else:
        rng = np.random.default_rng(seed)
        dist = np.zeros(perms)
        for sc, f in per:
            idx = rng.random((perms, len(sc))).argsort(axis=1)[:, :f]
            dist += sc[idx].sum(axis=1)
        dev = np.abs(dist - exp)
        p = float((1 + np.sum(dev >= abs(obs - exp) - 1e-12)) / (1 + perms))
        method = f"monte carlo ({perms}, seed {seed})"
    strata = [(len(sc), f) for sc, f in per]
    return {"p": p, "stat": round(float(obs), 6), "expected": round(float(exp), 6), "method": method, "best_p": _best_p(strata),
            "strata": strata}


def holm(ps, alpha):
    """ps: {name: p or None} -> {name: (rejected, threshold)}; None is never rejected and takes no rank."""
    named = sorted(((p, k) for k, p in ps.items() if p is not None))
    m, out, going = len(named), {}, True
    for i, (p, k) in enumerate(named):
        thr = alpha / (m - i)
        going = going and p <= thr
        out[k] = (going, thr)
    for k, p in ps.items():
        if p is None: out[k] = (False, None)
    return out


def judge(sessions, alpha=0.05, perms=20000, seed=46):
    """sessions: {session: {"near": [measures...], "far": [measures...]}} where measures = {name: value}. Returns the verdict."""
    if not 0.0 < alpha < 0.5: raise ValueError(f"alpha {alpha}: must be in (0, 0.5)")
    if perms < 1000: raise ValueError(f"perms {perms}: under 1000 the smallest Monte Carlo p sits above Holm's thresholds - nothing could fail")
    names = []
    for s in sessions.values():
        for m in s["near"] + s["far"]:
            for k in m:
                if k not in names: names.append(k)
    tests = {}
    for k in names:
        groups, shifts = [], []
        for s in sessions.values():
            def vals(rs):
                return [NEVER if (k.endswith("rest_t") and r.get(k) is None) else r.get(k) for r in rs
                        if r.get(k) is not None or k.endswith("rest_t")]
            a, b = vals(s["near"]), vals(s["far"])
            groups.append((a, b))
            if a and b: shifts.append(float(np.median(b) - np.median(a)))
        for kind in ("location", "spread"):
            t = perm_test(groups, kind, perms, seed)
            t["median_shift"] = None if not shifts else round(float(np.median(shifts)), 6)   # far - near, the sessions' median
            tests[f"{k}:{kind}"] = t
    m = sum(1 for t in tests.values() if t["p"] is not None)
    h = holm({k: t["p"] for k, t in tests.items()}, alpha)
    strict = alpha / m if m else None
    for k, t in tests.items():
        t["rejected"], t["holm_threshold"] = h[k]
        t["powered"] = t["best_p"] is not None and strict is not None and t["best_p"] <= strict
    rejected = [k for k, t in tests.items() if t["rejected"]]
    weak = [k for k, t in tests.items() if not t["powered"]]
    verdict = "FAIL" if rejected else ("PASS" if not weak else "UNDERPOWERED")
    return {"verdict": verdict, "rejected": rejected, "underpowered": weak, "alpha": alpha, "tests_m": m, "strict_threshold": strict,
            "perms": perms, "seed": seed, "sessions": {s: {"near": len(v["near"]), "far": len(v["far"])} for s, v in sessions.items()},
            "tests": tests}
