"""The six ACS part types: curve, spike, membrane, fin, eye, paw.

Port notes vs anyCreature engine/core/compile.js:
* curve/spike segment headings accumulate rise/fall/ahead/behind bends
  (N degrees toward the named world axis); `coil` is not ported (warned)
* curve/spike voxels bind RIGID to the host bone (spec tracks cannot target
  auto-generated segment names, and horns/claws don't articulate)
* membrane/fin rasterize as exactly-1-voxel-thick sheets via the dominant
  normal axis (double-sidedness collapses — correct for voxels)
* eye: ONE entry emits BOTH sides (mirror across the spine plane)
* paw: a literal Box at the host joint
"""
from __future__ import annotations

import math

from .joints import mirror_name
from .spec import SpecError, hex_to_rgb
from .sweep import (Sample, _add, _cross, _dot, _len, _norm, _scale, _sub,
                    boundary_point, frames_of)
from .voxelize import stamp_sheet, stamp_volume

_AXES = {"rise": (0.0, 1.0, 0.0), "fall": (0.0, -1.0, 0.0),
         "ahead": (0.0, 0.0, 1.0), "behind": (0.0, 0.0, -1.0)}


def _bend(heading, axis, deg):
    n = _sub(axis, _scale(heading, _dot(axis, heading)))
    if _len(n) < 1e-9:
        return heading
    n = _norm(n)
    a = math.radians(deg)
    return _norm(_add(_scale(heading, math.cos(a)), _scale(n, math.sin(a))))


def _mirror_x(p):
    return (-p[0], p[1], p[2])


def _polyline_point(pts, arcs, total, frac):
    a = max(0.0, min(1.0, frac)) * total
    i = 0
    while i < len(arcs) - 2 and a > arcs[i + 1]:
        i += 1
    seg = (arcs[i + 1] - arcs[i]) or 1.0
    f = (a - arcs[i]) / seg
    return _add(pts[i], _scale(_sub(pts[i + 1], pts[i]), f)), i, f


def _polyline_arcs(pts):
    arcs = [0.0]
    for i in range(1, len(pts)):
        arcs.append(arcs[-1] + _len(_sub(pts[i], pts[i - 1])))
    return arcs, (arcs[-1] or 1.0)


# ---------------------------------------------------------------------------
# curve / spike
# ---------------------------------------------------------------------------

def _curve_samples(part, start, bone, step):
    heading = _norm(part.get("dir", [0, 0, 1]))
    pos = start
    pts = [pos]
    radii = [None]
    for seg in part.get("segments", []):
        for key in ("rise", "fall", "ahead", "behind"):
            if key in seg:
                heading = _bend(heading, _AXES[key], float(seg[key]))
        if "coil" in seg:
            pass  # not ported; emit.compile_spec warns once
        end = _add(pos, _scale(heading, float(seg["len"])))
        pts.append(end)
        radii.append(float(seg["r"]))
        pos = end
    if radii[0] is None:
        radii[0] = radii[1] if len(radii) > 1 and radii[1] else 0.01
    if part.get("segments") and part["segments"][-1].get("taper"):
        radii[-1] = radii[-1] * 0.25
    if len(pts) < 2:
        return []

    arcs, total = _polyline_arcs(pts)
    frames, tans = frames_of(pts, None)
    samples = []
    n = max(2, int(math.ceil(total / step)))
    for m in range(n + 1):
        frac = m / n
        p, i, f = _polyline_point(pts, arcs, total, frac)
        r = radii[i] + (radii[i + 1] - radii[i]) * f
        u, w = frames[i]
        samples.append(Sample(p, u, w, tans[i], frac, r, r,
                              {"exp": 2.0, "bias": 0.0, "roll": 0.0, "section": None},
                              bone))
    return samples


def build_curve(part, sk, joints, grid, rgb, step, mirrored_pass=False):
    host = part["host"]
    offset = part.get("offset", [0, 0, 0])
    start = _add(joints[host], tuple(float(x) for x in offset))
    samples = _curve_samples(part, start, host, step)
    stamp_volume(grid, samples, rgb, None, step)
    if part.get("mirrored"):
        rhost = mirror_name(host)
        if rhost not in sk.index:
            rhost = host  # host on the axis: mirror geometry only
        m = [Sample(_mirror_x(s.pos), _mirror_x(s.U), _mirror_x(s.W),
                    _mirror_x(s.T), s.t, s.rw, s.rh, s.opts, rhost)
             for s in samples]
        stamp_volume(grid, m, rgb, None, step)


# ---------------------------------------------------------------------------
# eye / fin anchoring on a host volume
# ---------------------------------------------------------------------------

def _anchor_frame(anchor, volume_eval):
    """Resolve {chain, t, around} to (world pos, sample) on the host volume
    surface. volume_eval: chain name -> samples list from the sweep."""
    chain = anchor["chain"]
    if chain not in volume_eval:
        raise SpecError(f"anchor chain '{chain}' has no volume to anchor on")
    samples = volume_eval[chain]
    t = float(anchor.get("t", 0.5))
    s = min((sm for sm in samples if 0.0 <= sm.t <= 1.0),
            key=lambda sm: abs(sm.t - t))
    u, v = boundary_point(s.rw, s.rh, s.opts, float(anchor.get("around", 0.0)))
    pos = _add(s.pos, _add(_scale(s.U, u), _scale(s.W, v)))
    radial = _norm(_add(_scale(s.U, u), _scale(s.W, v)))
    return pos, s, radial


def build_eye(part, sk, grid, rgb, direct_boxes):
    pos, s, _ = _anchor_frame(part["anchor"], part["_volume_eval"])
    host = part["host"]
    size = 2.0 * float(part.get("size", 0.03))
    direct_boxes.append((host, (size, size, size), pos, rgb))
    direct_boxes.append((host, (size, size, size), _mirror_x(pos), rgb))


def build_paw(part, sk, joints, rgb, direct_boxes):
    host = part["host"]
    size = tuple(float(x) for x in part["size"])
    direct_boxes.append((host, size, joints[host], rgb))
    if part.get("mirrored"):
        rhost = mirror_name(host)
        bone = rhost if rhost in sk.index else host
        direct_boxes.append((bone, size, _mirror_x(joints[host]), rgb))


def build_fin(part, sk, grid, rgb, step):
    pos, s, radial = _anchor_frame(part["anchor"], part["_volume_eval"])
    host = part["host"]
    conform = part.get("conform", True)
    if conform:
        n_axis = radial
        u_axis = s.T
        v_axis = _norm(_cross(n_axis, u_axis))
    else:
        u_axis = _norm(part.get("udir", [0, 0, 1]))
        v = part.get("vdir", [0, 1, 0])
        v_axis = _norm(_sub(v, _scale(u_axis, _dot(v, u_axis))))
        n_axis = _norm(_cross(u_axis, v_axis))
    pts2 = [(float(p[0]), float(p[1])) for p in part["points"]]

    def in_poly(x, y):
        inside = False
        j = len(pts2) - 1
        for i in range(len(pts2)):
            xi, yi = pts2[i]
            xj, yj = pts2[j]
            if (yi > y) != (yj > y) and \
                    x < (xj - xi) * (y - yi) / ((yj - yi) or 1e-12) + xi:
                inside = not inside
            j = i
        return inside

    us = [p[0] for p in pts2]
    vs_ = [p[1] for p in pts2]
    fine = step * 0.5
    points = []
    nu = max(2, int(math.ceil((max(us) - min(us)) / fine)))
    nv = max(2, int(math.ceil((max(vs_) - min(vs_)) / fine)))
    for iu in range(nu + 1):
        pu = min(us) + (max(us) - min(us)) * iu / nu
        for iv in range(nv + 1):
            pv = min(vs_) + (max(vs_) - min(vs_)) * iv / nv
            if not in_poly(pu, pv):
                continue
            wp = _add(pos, _add(_scale(u_axis, pu), _scale(v_axis, pv)))
            points.append((wp, host, rgb, n_axis))
    stamp_sheet(grid, points)
    if part.get("mirrored"):
        rhost = mirror_name(host)
        bone = rhost if rhost in sk.index else host
        mpoints = [(_mirror_x(p), bone, c, _mirror_x(n)) for p, _, c, n in points]
        stamp_sheet(grid, mpoints)


def build_membrane(part, sk, joints, grid, rgb, step):
    chains = sk.chains
    ribs = []
    for rib in part["ribs"]:
        names = chains[rib["chain"]]
        pts = [joints[n] for n in names]
        arcs, total = _polyline_arcs(pts)
        ribs.append((pts, arcs, total, names, float(rib.get("shorten", 0.0))))
    cusp = float(part.get("cusp", 0.0))
    fine = step * 0.5
    all_points = []
    for i in range(len(ribs) - 1):
        (pa, aa, ta, na, sha) = ribs[i]
        (pb, ab, tb, nb, shb) = ribs[i + 1]
        gap = _len(_sub(pb[0], pa[0])) + _len(_sub(pb[-1], pa[-1]))
        n_b = max(3, int(math.ceil(max(gap, 0.05) / fine)))
        n_a = max(3, int(math.ceil(max(ta, tb) / fine)))
        for bi in range(n_b + 1):
            b = bi / n_b
            a_max = 1.0 - cusp * 4.0 * b * (1.0 - b)  # trailing-edge scoop
            for ai in range(n_a + 1):
                a = (ai / n_a) * a_max
                qa, ia, fa = _polyline_point(pa, aa, ta, a * (1.0 - sha))
                qb, _, _ = _polyline_point(pb, ab, tb, a * (1.0 - shb))
                p = _add(_scale(qa, 1.0 - b), _scale(qb, b))
                # dominant rib joint for this point
                names_d = na if b < 0.5 else nb
                idx = ia if b < 0.5 else min(ia, len(names_d) - 2)
                bone = names_d[idx] if fa < 0.5 else names_d[min(idx + 1, len(names_d) - 1)]
                # sheet normal: rib direction x rib-to-rib direction
                da = _norm(_sub(pa[-1], pa[0]))
                ab_dir = _norm(_sub(qb, qa)) if _len(_sub(qb, qa)) > 1e-9 else (0, 0, 1)
                n = _norm(_cross(da, ab_dir))
                all_points.append((p, bone, rgb, n))
    stamp_sheet(grid, all_points)
    if part.get("mirrored"):
        m = []
        for p, bone, c, n in all_points:
            rb = mirror_name(bone)
            m.append((_mirror_x(p), rb if rb in sk.index else bone, c, _mirror_x(n)))
        stamp_sheet(grid, m)


# ---------------------------------------------------------------------------
# dispatch
# ---------------------------------------------------------------------------

def build_parts(spec, sk, joints, grid, volume_eval, step, direct_boxes, warnings):
    palette = spec["palette"]
    for part in spec.get("parts", []):
        rgb = hex_to_rgb(palette[part["material"]]["color"])
        ptype = part["type"]
        part = dict(part)
        part["_volume_eval"] = volume_eval
        if ptype in ("curve", "spike"):
            if any("coil" in seg for seg in part.get("segments", [])):
                warnings.append(f"part on '{part.get('host')}': 'coil' is not ported, ignored")
            build_curve(part, sk, joints, grid, rgb, step)
        elif ptype == "eye":
            build_eye(part, sk, grid, rgb, direct_boxes)
        elif ptype == "paw":
            build_paw(part, sk, joints, rgb, direct_boxes)
        elif ptype == "fin":
            build_fin(part, sk, grid, rgb, step)
        elif ptype == "membrane":
            build_membrane(part, sk, joints, grid, rgb, step)
