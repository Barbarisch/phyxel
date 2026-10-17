"""WaterCore 26.20: a 1-D reference for the stepped channel - gradually varied flow over flat reaches (Manning friction),
marched UPSTREAM from the free overfall at the channel's end (critical depth at the brink's control), and at each drop either
(E) energy conserved, or (M) momentum with a hydrostatic step face (the upstream surface's pressure on the riser).
Mean depth over reaches 1-3, as the rig measures."""
import math, sys

g, n, S = 9.81, 0.035, 0.005

def run(q, drop, reach, reaches=5, rule="M", dx=0.01):
    dc = (q * q / g) ** (1 / 3)
    sf = lambda d: (n * q) ** 2 / d ** (10 / 3)
    E = lambda d: d + q * q / (2 * g * d * d)
    M = lambda d: q * q / (g * d) + d * d / 2
    s_tilt = S - drop / reach        # the slope the steps do not carry (a smooth tilt, as the rig's tilted frame)
    d = dc * 1.0001                  # at the brink (the overfall's control; the brink itself is ~0.715 dc but within ~4 dc)
    prof = [[] for _ in range(reaches)]
    for r in range(reaches - 1, -1, -1):
        x = reach
        while x > 0:
            step = dx
            while True:   # substep so one move changes the depth by <= 0.1 mm (the slope is singular at critical depth)
                fr2 = q * q / (g * d ** 3)
                dd = (s_tilt - sf(d)) / (1 - fr2)        # dd/dx; marching upstream: d(x - dx) = d - dd dx
                h_ = min(step, 1e-4 / max(abs(dd), 1e-12))
                d = max(d - dd * h_, dc * 1.0001)
                step -= h_
                if step <= 1e-12: break
            x -= dx
            prof[r].append(d)
        if r > 0 and drop > 0:      # the drop between reach r-1 (upstream) and r: d is the depth at reach r's head
            d2 = d
            if rule == "E":
                target = E(d2) - drop    # E1 + drop = E2
                lo, hi = dc, 5.0
            else:   # M1 + drop (d1 + drop / 2) = M2 : the riser carries the upstream surface's hydrostatic load
                target = None
                lo, hi = dc, 5.0
            f = (lambda d1: E(d1) - target) if rule == "E" else (lambda d1: M(d1) + drop * (d1 + drop / 2) - M(d2))
            if f(lo) > 0 and f(hi) > 0:
                d = dc   # no subcritical solution: the drop is a free overfall (critical at its brink)
            else:
                for _ in range(100):
                    mid = 0.5 * (lo + hi)
                    if (f(mid) > 0) == (f(hi) > 0): hi = mid
                    else: lo = mid
                d = max(0.5 * (lo + hi), dc)
    mean = lambda r: sum(prof[r]) / len(prof[r])
    mid = sum(sum(prof[r]) for r in (1, 2, 3)) / sum(len(prof[r]) for r in (1, 2, 3))
    return mid, [mean(r) for r in range(reaches)]

for q in (0.1, 0.25):
    dn = (q * n / math.sqrt(S)) ** 0.6
    print(f"q {q}: Manning {dn:.4f}  dc {(q*q/g)**(1/3):.4f}")
    for drop, reach in ((0.0, 200/9), (1/9, 200/9), (2/9, 400/9), (1/18, 100/9)):
        for rule in ("E", "M"):
            mid, rs = run(q, drop, reach, rule=rule)
            print(f"  drop {drop:.3f} reach {reach:5.1f} rule {rule}: mid {mid:.4f} = {mid/dn:.3f} x Manning; reaches " + " ".join(f"{x:.4f}" for x in rs))
