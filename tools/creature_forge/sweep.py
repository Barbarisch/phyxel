"""Swept-volume membership: spine sampling, frames, profiles, sections.

Port of anyCreature engine/core/{geometry.js, section.js} re-targeted from
triangle rings to a voxel membership function. Key fidelity notes:

* the spine between joints is PIECEWISE LINEAR (curvature comes from dense
  sampling + the Catmull-Rom radius profile), do not spline it
* the profile Catmull-Rom is uniformly parameterized (neighbour t spacing
  deliberately ignored); a span bracketed by a `sharp` row is linear
* frames: "up" = world-aligned with a deterministic sign fix, "ground" =
  world-down projected, default = rotation-minimizing parallel transport
* the mesh corner bevel-skip is intentionally DROPPED: it prevented mesh
  self-intersection at sharp bends, which membership-union voxels cannot
  suffer from (slight outer-elbow bulge vs the original is accepted)
* superellipse: implicit |u/rw|^exp + |v/rh|^exp <= 1 with the vertical
  `bias` as a piecewise scale of the top/bottom half
"""
from __future__ import annotations

import math
from dataclasses import dataclass


# ---------------------------------------------------------------------------
# small vector helpers (tuples)
# ---------------------------------------------------------------------------

def _sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def _add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def _scale(a, s):
    return (a[0] * s, a[1] * s, a[2] * s)


def _dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def _len(a):
    return math.sqrt(_dot(a, a))


def _norm(a):
    l = _len(a)
    if l < 1e-12:
        return (0.0, 0.0, 1.0)
    return (a[0] / l, a[1] / l, a[2] / l)


def _rodrigues(v, ax, ang):
    c, s = math.cos(ang), math.sin(ang)
    return _add(_add(_scale(v, c), _scale(_cross(ax, v), s)),
                _scale(ax, _dot(ax, v) * (1.0 - c)))


# ---------------------------------------------------------------------------
# profile evaluation (port of lerpProfile / crProfile)
# ---------------------------------------------------------------------------

_DEF_OPTS = {"exp": 2.0, "bias": 0.0, "roll": 0.0, "section": None, "sharp": False}


def _rows(profile):
    out = []
    for r in profile:
        opts = dict(_DEF_OPTS)
        if len(r) > 3 and isinstance(r[3], dict):
            opts.update(r[3])
        out.append((float(r[0]), float(r[1]), float(r[2]), opts))
    return out


def _mix_opt(a, b, f, key):
    d = _DEF_OPTS[key]
    va = a.get(key, d) if a.get(key) is not None else d
    vb = b.get(key, d) if b.get(key) is not None else d
    return va + (vb - va) * f


def lerp_profile(rows, t):
    if t <= rows[0][0]:
        r = rows[0]
        return r[1], r[2], dict(r[3])
    if t >= rows[-1][0]:
        r = rows[-1]
        return r[1], r[2], dict(r[3])
    for i in range(len(rows) - 1):
        if rows[i][0] <= t <= rows[i + 1][0]:
            break
    a, b = rows[i], rows[i + 1]
    span = (b[0] - a[0]) or 1.0
    f = (t - a[0]) / span
    w = a[1] + (b[1] - a[1]) * f
    h = a[2] + (b[2] - a[2]) * f
    opts = {
        "exp": _mix_opt(a[3], b[3], f, "exp"),
        "bias": _mix_opt(a[3], b[3], f, "bias"),
        "roll": _mix_opt(a[3], b[3], f, "roll"),
        "section": (a[3]["section"] if f < 0.5 else b[3]["section"]),
    }
    return w, h, opts


def cr_profile(rows, t):
    """Uniformly-parameterized Catmull-Rom on w/h; opts always linear; a span
    bracketed by a `sharp` row falls back to linear (hard silhouette break)."""
    if t <= rows[0][0] or t >= rows[-1][0]:
        return lerp_profile(rows, t)
    for i in range(len(rows) - 1):
        if rows[i][0] <= t <= rows[i + 1][0]:
            break
    p1, p2 = rows[i], rows[i + 1]
    if p1[3].get("sharp") or p2[3].get("sharp"):
        return lerp_profile(rows, t)
    p0 = rows[max(0, i - 1)]
    p3 = rows[min(len(rows) - 1, i + 2)]
    span = (p2[0] - p1[0]) or 1.0
    f = (t - p1[0]) / span

    def cr(a, b, c, d):
        return 0.5 * (2 * b + (-a + c) * f + (2 * a - 5 * b + 4 * c - d) * f * f
                      + (-a + 3 * b - 3 * c + d) * f * f * f)

    w = max(0.004, cr(p0[1], p1[1], p2[1], p3[1]))
    h = max(0.004, cr(p0[2], p1[2], p2[2], p3[2]))
    _, _, opts = lerp_profile(rows, t)
    return w, h, opts


# ---------------------------------------------------------------------------
# frames
# ---------------------------------------------------------------------------

def _tangents(pts):
    n = len(pts)
    if n == 1:
        return [(0.0, 0.0, 1.0)]
    tans = []
    for i in range(n):
        if i == 0:
            tans.append(_norm(_sub(pts[1], pts[0])))
        elif i == n - 1:
            tans.append(_norm(_sub(pts[-1], pts[-2])))
        else:
            tans.append(_norm(_add(_norm(_sub(pts[i + 1], pts[i])),
                                   _norm(_sub(pts[i], pts[i - 1])))))
    return tans


def frames_of(pts, mode):
    """Return [(U, W)] per sample. U = width/side axis, W = height/up axis."""
    tans = _tangents(pts)
    out = []
    if mode == "up":
        for t in tans:
            up = (0.0, 0.0, 1.0) if abs(t[1]) > 0.99 else (0.0, 1.0, 0.0)
            u = _norm(_cross(up, t))
            w = _norm(_cross(t, u))
            # deterministic sign fix
            for ax in ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0)):
                d = _dot(u, ax)
                if abs(d) > 0.1:
                    if d < 0:
                        u = _scale(u, -1.0)
                        w = _scale(w, -1.0)
                    break
            out.append((u, w))
    elif mode == "ground":
        for t in tans:
            d = (0.0, -1.0, 0.0)
            u = _sub(d, _scale(t, _dot(d, t)))
            if _len(u) < 0.15:
                d = (0.0, 0.0, -1.0)
                u = _sub(d, _scale(t, _dot(d, t)))
            u = _norm(u)
            w = _norm(_cross(t, u))
            out.append((u, w))
    else:  # rotation-minimizing parallel transport
        ref = (0.0, 1.0, 0.0) if abs(tans[0][1]) < 0.9 else (1.0, 0.0, 0.0)
        u = _norm(_cross(tans[0], ref))
        w = _norm(_cross(tans[0], u))
        out.append((u, w))
        for s in range(1, len(tans)):
            ax = _cross(tans[s - 1], tans[s])
            if _len(ax) > 1e-6:
                ang = math.acos(max(-1.0, min(1.0, _dot(tans[s - 1], tans[s]))))
                u = _rodrigues(u, _norm(ax), ang)
            u = _norm(_sub(u, _scale(tans[s], _dot(u, tans[s]))))
            w = _norm(_cross(tans[s], u))
            out.append((u, w))
    return out, tans


# ---------------------------------------------------------------------------
# section membership
# ---------------------------------------------------------------------------

def _point_in_polygon(x, y, pts):
    inside = False
    n = len(pts)
    j = n - 1
    for i in range(n):
        xi, yi = pts[i]
        xj, yj = pts[j]
        if (yi > y) != (yj > y) and \
                x < (xj - xi) * (y - yi) / ((yj - yi) or 1e-12) + xi:
            inside = not inside
        j = i
    return inside


def inside_section(u, v, rw, rh, opts, sections):
    roll = opts.get("roll") or 0.0
    if roll:
        c, s = math.cos(-roll), math.sin(-roll)
        u, v = u * c - v * s, u * s + v * c
    bias = opts.get("bias") or 0.0
    if bias:
        v = v / (1.0 + bias) if v > 0 else v / (1.0 - bias)
    name = opts.get("section")
    if name:
        pts = [(px * rw, py * rh) for px, py in sections[name]]
        return _point_in_polygon(u, v, pts)
    e = opts.get("exp") or 2.0
    return abs(u / rw) ** e + abs(v / rh) ** e <= 1.0


def boundary_point(rw, rh, opts, angle_from_top_deg):
    """Point on the section boundary at anyCreature's `around` angle
    (0 = +W spine/top, 90 = +U side, 180 = belly). Returns (u, v)."""
    a_deg = (450.0 - angle_from_top_deg) % 360.0
    a = math.radians(a_deg)
    e = 2.0 / (opts.get("exp") or 2.0)
    ca, sa = math.cos(a), math.sin(a)
    u = math.copysign(abs(ca) ** e, ca) * rw
    v = math.copysign(abs(sa) ** e, sa) * rh
    bias = opts.get("bias") or 0.0
    if bias:
        v *= (1.0 + bias) if sa > 0 else (1.0 - bias)
    return u, v


# ---------------------------------------------------------------------------
# volume sampling
# ---------------------------------------------------------------------------

@dataclass
class Sample:
    pos: tuple
    U: tuple
    W: tuple
    T: tuple
    t: float          # arc fraction along the chain (may exit [0,1] in a dome cap)
    rw: float
    rh: float
    opts: dict
    joint: str        # dominant (max-weight) joint for this ring


def volume_samples(vol: dict, chain_names, world, step):
    """Dense samples along the chain at `step` arc spacing, with frames,
    profiles and dominant-joint attribution; dome caps append shrinking
    extension samples past the ends."""
    pts_j = [world[n] for n in chain_names]
    jarc = [0.0]
    for i in range(1, len(pts_j)):
        jarc.append(jarc[-1] + _len(_sub(pts_j[i], pts_j[i - 1])))
    total = jarc[-1] or 1.0
    rows = _rows(vol["profile"])
    mode = vol.get("frame")

    m_count = max(len(pts_j), int(round(total / step)))
    pts, ts, joints_dom = [], [], []
    for m in range(m_count + 1):
        a = m / m_count * total
        i = 0
        while i < len(jarc) - 2 and a > jarc[i + 1]:
            i += 1
        seg = (jarc[i + 1] - jarc[i]) or 1.0
        f = (a - jarc[i]) / seg
        pts.append(_add(pts_j[i], _scale(_sub(pts_j[i + 1], pts_j[i]), f)))
        ts.append(a / total)
        sf = f * f * (3.0 - 2.0 * f)  # smoothstep, as the ring skin uses
        joints_dom.append(chain_names[i] if sf < 0.5 else chain_names[i + 1])

    frames, tans = frames_of(pts, mode)
    samples = []
    for pos, (u, w), t, jn in zip(pts, frames, ts, joints_dom):
        rw, rh, opts = cr_profile(rows, t)
        tan = tans[len(samples) if len(samples) < len(tans) else -1]
        samples.append(Sample(pos, u, w, tan, t, rw, rh, opts, jn))

    # dome caps: extend past the ends with an elliptically shrinking profile
    caps = vol.get("caps", ["ngon", "ngon"])
    caps = [("fan" if c is True else "none" if c in (False, None) else c) for c in caps]
    ext = []
    for end, cap in ((0, caps[0]), (1, caps[1])):
        if cap != "dome":
            continue
        s0 = samples[0] if end == 0 else samples[-1]
        direction = _scale(s0.T, -1.0) if end == 0 else s0.T
        dome_len = 0.35 * max(s0.rw, s0.rh)
        n_ext = max(1, int(math.ceil(dome_len / step)))
        for k in range(1, n_ext + 1):
            frac = k / n_ext
            shrink = math.sqrt(max(0.0, 1.0 - frac * frac))
            if shrink <= 0.05:
                continue
            ext.append(Sample(
                _add(s0.pos, _scale(direction, frac * dome_len)),
                s0.U, s0.W, s0.T,
                (-frac if end == 0 else 1.0 + frac),
                s0.rw * shrink, s0.rh * shrink, dict(s0.opts), s0.joint))
    samples.extend(ext)
    return samples, total
