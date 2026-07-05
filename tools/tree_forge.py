#!/usr/bin/env python3
"""tree_forge — a single, unified, multi-resolution procedural tree generator.

Ground-up replacement for the pile of bespoke gen_tree.py archetypes. ONE algorithm:

  1. A branch SKELETON is grown by space colonization inside a tunable canopy envelope
     (cone/dome/column + size). Branches grow toward scattered attractor points, so the
     structure is genuinely recursive — trunk -> big limbs -> medium branches -> fine twigs —
     with branch COUNT driven by attractor density, not a hand-placed limb count.
  2. Segment RADII follow Murray's law (parent^2 ~= sum(child^2)): thick near the trunk,
     tapering to twigs.
  3. Everything is voxelized on a fine grid (subcube for wood, micro for twigs) and then
     HIERARCHICALLY COMPRESSED on emit: 729 same-material micros in a cube -> one C(ube); 27 in a
     subcube -> one S(ubcube); else M(icrocube). Resolution is therefore per-VOXEL, by distance
     from the surface: a thick trunk's solid INTERIOR collapses to cheap whole cubes while its
     curved SURFACE keeps a subcube shell (slopes, not 1-cube stairsteps), and twigs stay micro —
     no hand-tuned per-type switches.

Archetypes (pine, oak, giant, ...) are PARAMETER PRESETS of this one function; `detail` (attractor
count) is the performance dial. See docs/ProceduralTreeExpansionPlan.md.
"""

import math
import random

MICRO_PER_CUBE = 9          # 3 (cube->sub) * 3 (sub->micro); the engine's finest grid
SUB_PER_CUBE = 3


# --------------------------------------------------------------- vec helpers
def _add(a, b): return (a[0] + b[0], a[1] + b[1], a[2] + b[2])
def _sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def _mul(a, s): return (a[0] * s, a[1] * s, a[2] * s)
def _dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def _len(a): return math.sqrt(_dot(a, a))
def _norm(a):
    m = _len(a) or 1.0
    return (a[0] / m, a[1] / m, a[2] / m)


# ============================================================ multi-res voxels

class MicroVoxels:
    """Sparse voxel accumulator addressed in MICROCUBE units (1 cube = 9 micro per axis).
    Everything the generator draws lands here at micro precision; emit() compresses it back up
    to cubes/subcubes wherever a region is fully + uniformly filled."""

    __slots__ = ("v",)

    def __init__(self):
        self.v = {}            # (mx,my,mz) -> material   (integer micro coords, may be negative)

    def set(self, mx, my, mz, mat, overwrite=True):
        k = (mx, my, mz)
        if overwrite or k not in self.v:
            self.v[k] = mat

    def __len__(self):
        return len(self.v)


def emit(mv):
    """Compress the micro grid to C/S/M primitive lines. Returns (lines, counts, bounds_cubes).

    Hierarchy: a cube whose 729 micros are all present + one material -> C; else a subcube whose
    27 micros are all present + one material -> S; else each micro -> M."""
    if not mv.v:
        return [], (0, 0, 0), (0, 0, 0)

    def fdiv(a, b):
        return a // b                     # Python floor-division: correct for negatives

    # bucket micros by cube, storing in-cube micro index (0..8 per axis) -> material
    by_cube = {}
    for (mx, my, mz), mat in mv.v.items():
        ck = (fdiv(mx, MICRO_PER_CUBE), fdiv(my, MICRO_PER_CUBE), fdiv(mz, MICRO_PER_CUBE))
        by_cube.setdefault(ck, {})[(mx % MICRO_PER_CUBE, my % MICRO_PER_CUBE, mz % MICRO_PER_CUBE)] = mat

    ox = min(c[0] for c in by_cube)
    oy = min(c[1] for c in by_cube)
    oz = min(c[2] for c in by_cube)
    mxc = max(c[0] for c in by_cube)
    myc = max(c[1] for c in by_cube)
    mzc = max(c[2] for c in by_cube)

    c_lines, s_lines, m_lines = [], [], []
    for (cx, cy, cz), micros in sorted(by_cube.items(), key=lambda kv: (kv[0][1], kv[0][0], kv[0][2])):
        rx, ry, rz = cx - ox, cy - oy, cz - oz
        mats = set(micros.values())
        if len(micros) == MICRO_PER_CUBE ** 3 and len(mats) == 1:
            c_lines.append(f"C {rx} {ry} {rz} {next(iter(mats))}")
            continue
        # group this cube's micros by subcube (ux,uy,uz in 0..2)
        by_sub = {}
        for (mi, mj, mk), mat in micros.items():
            sk = (mi // SUB_PER_CUBE, mj // SUB_PER_CUBE, mk // SUB_PER_CUBE)
            by_sub.setdefault(sk, {})[(mi % SUB_PER_CUBE, mj % SUB_PER_CUBE, mk % SUB_PER_CUBE)] = mat
        for (ux, uy, uz), smicros in sorted(by_sub.items()):
            smats = set(smicros.values())
            if len(smicros) == SUB_PER_CUBE ** 3 and len(smats) == 1:
                s_lines.append(f"S {rx} {ry} {rz} {ux} {uy} {uz} {next(iter(smats))}")
            else:
                for (px, py, pz), mat in sorted(smicros.items()):
                    m_lines.append(f"M {rx} {ry} {rz} {ux} {uy} {uz} {px} {py} {pz} {mat}")

    counts = (len(c_lines), len(s_lines), len(m_lines))
    bounds = (mxc - ox + 1, myc - oy + 1, mzc - oz + 1)
    return c_lines + s_lines + m_lines, counts, bounds


# ============================================================ skeleton (space colonization)

def _envelope_points(rng, params):
    """Scatter `attractors` points inside the canopy envelope. Shape controls the silhouette:
    'dome' (broad oak crown), 'cone' (conifer), 'column' (redwood), 'sphere' (round). The envelope
    center/size are in cube units; `canopy_r` is the horizontal radius, `canopy_h` the vertical."""
    shape = params["envelope"]
    cx, cy, cz = params["crown_center"]
    rr = params["canopy_r"]
    hh = params["canopy_h"]
    pts = []
    n = params["attractors"]
    tries = 0
    while len(pts) < n and tries < n * 40:
        tries += 1
        # sample in the bounding cylinder then reject to the shape
        u = rng.uniform(-1, 1); w = rng.uniform(-1, 1); ev = rng.uniform(0, 1)
        px, pz = u * rr, w * rr
        rad = math.hypot(px, pz) / rr if rr else 0
        if shape == "cone":
            # radius shrinks linearly to the top
            if rad > (1.0 - ev):
                continue
            py = cy - hh + ev * 2 * hh
        elif shape == "column":
            if rad > 1.0:
                continue
            py = cy - hh + ev * 2 * hh
        elif shape == "sphere":
            py = cy - hh + ev * 2 * hh
            d = (px / rr) ** 2 + ((py - cy) / hh) ** 2 + (pz / rr) ** 2
            if d > 1.0:
                continue
        elif shape == "umbrella":
            # acacia: a wide, THIN, flat-topped crown slab held high; slightly lower at the rim
            if rad > 1.0:
                continue
            py = cy + hh * (0.35 + 0.65 * ev) * (1.0 - 0.3 * rad * rad)
        elif shape == "weeping":
            # willow: a dome cap plus a hanging CURTAIN — attractors descend on a loose
            # cylindrical shell from the crown rim toward the ground, so limbs grow out
            # over the rim and then chase them DOWN (hanging foliage).
            if ev < 0.3:
                if rad > 1.0:
                    continue
                py = cy + (ev / 0.3) * hh                    # top cap
            else:
                if not (0.55 <= rad <= 1.05):
                    continue                                  # shell only
                t = (ev - 0.3) / 0.7
                py = (cy + hh) - t * (cy + hh - 2.0)          # descend to ~2 cubes off the ground
        else:  # dome: oblate ellipsoid, fuller in the upper 2/3
            py = cy - hh * 0.4 + ev * hh * 1.4
            d = (px / rr) ** 2 + ((py - cy) / hh) ** 2 + (pz / rr) ** 2
            if d > 1.0:
                continue
        pts.append((cx + px, py, cz + pz))
    return pts


def grow_skeleton(rng, params):
    """Space colonization. Returns nodes = list of dicts {pos, parent, children, radius}. Branches
    grow from the root toward attractor points, competing for them, producing a recursive
    trunk->limbs->twigs structure whose branch COUNT is set by attractor density, not by hand."""
    attractors = _envelope_points(rng, params)
    step = params["step"]
    infl = params["influence"]
    kill = params["kill"]
    up_trop = params["up_tropism"]
    jitter = params["jitter"]

    crook = params["crook"]
    root = (params["crown_center"][0], 0.0, params["crown_center"][2])
    nodes = [{"pos": root, "parent": -1}]

    # pre-grow the trunk stem up to the crown base. `crook` drives a persistent lateral drift (a
    # damped random walk), so the bole leans/wanders instead of being a ramrod-straight pole.
    base_y = params["crown_center"][1] - params["canopy_h"]
    y = 0.0
    px, pz = root[0], root[2]
    dvx, dvz = 0.0, 0.0
    while y < base_y - step:
        y += step
        dvx = dvx * 0.82 + rng.uniform(-crook, crook) * step * 0.5
        dvz = dvz * 0.82 + rng.uniform(-crook, crook) * step * 0.5
        px += dvx; pz += dvz
        nodes.append({"pos": (px, y, pz), "parent": len(nodes) - 1})

    max_iter = params["max_iter"]
    for _ in range(max_iter):
        if not attractors:
            break
        # nearest node per attractor (within influence)
        pulls = {}   # node_idx -> summed direction
        used = [False] * len(attractors)
        for ai, a in enumerate(attractors):
            best, bestd = -1, infl
            for ni, nd in enumerate(nodes):
                d = _len(_sub(a, nd["pos"]))
                if d < bestd:
                    bestd, best = d, ni
            if best >= 0:
                pulls[best] = _add(pulls.get(best, (0, 0, 0)), _norm(_sub(a, nodes[best]["pos"])))
                used[ai] = True
        if not pulls:
            break
        grew = False
        ck = crook * 0.35
        for ni, dsum in pulls.items():
            d = _norm(dsum)
            d = _norm((d[0] + rng.uniform(-jitter, jitter) + rng.uniform(-ck, ck),
                       d[1] + up_trop + rng.uniform(-jitter, jitter),
                       d[2] + rng.uniform(-jitter, jitter) + rng.uniform(-ck, ck)))
            newpos = _add(nodes[ni]["pos"], _mul(d, step))
            nodes.append({"pos": newpos, "parent": ni})
            grew = True
        if not grew:
            break
        # kill attractors reached by any node
        attractors = [a for a in attractors
                      if all(_len(_sub(a, nd["pos"])) > kill for nd in nodes[-len(pulls):])
                      and (min((_len(_sub(a, nd["pos"])) for nd in nodes), default=kill + 1) > kill)]
    return nodes


def assign_radii(nodes, params):
    """Murray's law radii, bottom-up: leaf edges = r_min; a node's radius^n = sum of child radius^n
    (n~2.3). Gives a thick trunk tapering smoothly to fine twigs."""
    r_min = params["r_min"]
    n_exp = params["murray_n"]
    children = [[] for _ in nodes]
    for i, nd in enumerate(nodes):
        if nd["parent"] >= 0:
            children[nd["parent"]].append(i)
    rad = [r_min] * len(nodes)
    # proper post-order via child radii: iterate until stable-ish (tree is shallow enough)
    for i in sorted(range(len(nodes)), reverse=True):
        ch = children[i]
        if ch:
            rad[i] = (sum(rad[c] ** n_exp for c in ch)) ** (1.0 / n_exp)
        else:
            rad[i] = r_min
    # AFFINE remap radii so the trunk hits trunk_r while TWIGS stay at r_min (a uniform scale would
    # inflate the twigs on big trees, pushing them past the leaf threshold -> a bald giant). Map
    # r_min -> r_min and root_radius -> trunk_r linearly.
    base_r = params["trunk_r"]
    root_r = max(rad[0], r_min + 1e-6) if rad else r_min
    span = root_r - r_min
    scale = (base_r - r_min) / span if span > 1e-6 else 1.0
    for i, nd in enumerate(nodes):
        nd["radius"] = max(r_min, r_min + (rad[i] - r_min) * scale)
        nd["children"] = children[i]
    # elephant-foot flare: widen the trunk over the first root_flare_h cubes above ground
    flare, fh = params["root_flare"], params["root_flare_h"]
    for nd in nodes:
        y = nd["pos"][1]
        if 0 <= y < fh:
            nd["radius"] *= 1.0 + (flare - 1.0) * (1.0 - y / fh) ** 1.5
    return nodes


def add_roots(nodes, rng, params):
    """Append exposed root spurs fanning out from the base — thick at the trunk, tapering along (and
    just into) the ground. The trunk stays straight; the roots provide the natural splayed footing."""
    r_min = params["r_min"]
    step = params["step"]
    base = nodes[0]["pos"]
    base_r = params["trunk_r"] * params["root_flare"]
    spread = params["root_spread"]
    rc = rng.randint(*params["root_count"])
    for k in range(rc):
        ang = 2 * math.pi * (k + rng.uniform(-0.35, 0.35)) / rc
        dx, dz = math.cos(ang), math.sin(ang)
        px, py, pz = base
        parent = 0
        n = rng.randint(*params["root_len"])
        rr = base_r * rng.uniform(0.7, 1.0)
        for s in range(n):
            t = (s + 1) / n
            wob = rng.uniform(-0.25, 0.25)                     # heavy randomness
            px += (dx + wob) * step * spread
            pz += (dz - wob) * step * spread
            py = base[1] - t * rng.uniform(0.05, 0.3)          # hug the ground (y=0 is the floor —
            nodes.append({"pos": (px, py, pz), "parent": parent, "children": [],
                          "radius": max(r_min, rr * (1.0 - 0.8 * t)),
                          "root": True})                       # roots never grow foliage
            parent = len(nodes) - 1
    return nodes


def add_fronds(nodes, rng, params):
    """Palm frond crown: leaf-material arcs radiating from the trunk apex — each rises, arcs
    over, and droops. The fronds ARE the palm's foliage (palm sets leaf_mat="" so add_foliage
    stays out of the way); per-node `mat` overrides the log material in rasterization."""
    fr = params.get("fronds")
    if not fr:
        return nodes
    apex_i = max(range(len(nodes)), key=lambda i: nodes[i]["pos"][1])
    ax, ay, az = nodes[apex_i]["pos"]
    step = params["step"]
    length = params["frond_len"]
    mat = params["frond_mat"]
    r_min = params["r_min"]
    for k in range(rng.randint(*fr)):
        ang = 2 * math.pi * (k + rng.uniform(-0.3, 0.3)) / fr[1]
        dx, dz = math.cos(ang), math.sin(ang)
        elev = rng.uniform(0.55, 0.95)                 # initial upward pitch
        px, py, pz = ax, ay, az
        parent = apex_i
        nseg = max(3, int(length / step))
        rr = params["frond_r"] * rng.uniform(0.8, 1.1)
        for s in range(nseg):
            t = (s + 1) / nseg
            e = max(elev - t * t * 2.1, -1.2)          # arc over, then droop
            px += dx * math.cos(e) * step
            pz += dz * math.cos(e) * step
            py += math.sin(e) * step
            nodes.append({"pos": (px, py, pz), "parent": parent, "children": [],
                          "radius": max(r_min, rr * (1.0 - 0.75 * t)),
                          "mat": mat, "frond": True})
            parent = len(nodes) - 1
    return nodes


# ============================================================ voxelization

def _res_for_radius(r_cube):
    """Pick the RASTERIZATION grid (in micro units) from a segment's radius in cube units.
    Thick wood rasterizes at SUBCUBE resolution — never cube — so its curved surface carries a
    subcube shell (slopes, not 1-cube stairsteps); emit() re-compresses the solid interior back to
    whole C's for free, so cost stays cube-cheap where it's invisible. Twigs stay microcube.
    (Resolution is thus per-VOXEL — interior C, surface S — not per-segment.)"""
    if r_cube >= 0.16:
        return SUB_PER_CUBE            # 3 -> subcube surface, emit() gives cube interior
    return 1                           # 1 -> microcube


def _fill_voxel(mv, ox, oy, oz, vs, mat):
    """Fill every micro inside the vs-sized voxel whose min corner is (ox,oy,oz) (grid-aligned)."""
    v = mv.v
    for ix in range(vs):
        for iy in range(vs):
            for iz in range(vs):
                v[(ox + ix, oy + iy, oz + iz)] = mat


def rasterize_branches(nodes, mv, params):
    """Draw each branch segment as a tapered capsule, rasterized at the resolution its radius earns.
    Thick segments fill whole grid-aligned CUBES (emit as C); medium fill SUBCUBES (S); twigs fill
    MICROCUBES (M). A limb therefore tapers cube -> sub -> micro along its length."""
    log = params["log_mat"]
    MP = MICRO_PER_CUBE
    for nd in nodes:
        p = nd["parent"]
        if p < 0:
            continue
        a = _mul(nodes[p]["pos"], MP)
        b = _mul(nd["pos"], MP)
        ra = nodes[p]["radius"] * MP
        rb = nd["radius"] * MP
        seg_mat = nd.get("mat", log)                 # fronds etc. override the wood material
        vs = _res_for_radius(max(nodes[p]["radius"], nd["radius"]))
        ab = _sub(b, a)
        abl2 = _dot(ab, ab) or 1.0
        rmax = max(ra, rb) + vs
        # snap the scan box to the vs grid
        lo = [int(math.floor((min(a[i], b[i]) - rmax) / vs)) * vs for i in range(3)]
        hi = [int(math.ceil((max(a[i], b[i]) + rmax) / vs)) * vs for i in range(3)]
        half = vs * 0.5
        for vx in range(lo[0], hi[0] + 1, vs):
            for vy in range(lo[1], hi[1] + 1, vs):
                for vz in range(lo[2], hi[2] + 1, vs):
                    cx = vx + half; cy = vy + half; cz = vz + half   # voxel center
                    apx = cx - a[0]; apy = cy - a[1]; apz = cz - a[2]
                    tt = (apx * ab[0] + apy * ab[1] + apz * ab[2]) / abl2
                    tt = 0.0 if tt < 0 else (1.0 if tt > 1 else tt)
                    dx = apx - ab[0] * tt; dy = apy - ab[1] * tt; dz = apz - ab[2] * tt
                    r = ra + (rb - ra) * tt
                    if dx * dx + dy * dy + dz * dz <= (r + half * 0.3) ** 2:
                        _fill_voxel(mv, vx, vy, vz, vs, seg_mat)


def add_foliage(nodes, mv, rng, params):
    """Leaf clusters on the TWIG TIPS (and thinnest branches), rasterized at `leaf_res` (subcube by
    default — the perf lever: subcube leaves are ~27x fewer primitives than micro leaves, at some
    cost to fineness). Wood is drawn first and wins, so fine twigs poke through the canopy."""
    leaf = params["leaf_mat"]
    if not leaf:
        return
    MP = MICRO_PER_CUBE
    vs = params["leaf_res"]                 # 9 cube / 3 subcube / 1 micro
    lr = params["leaf_r"] * MP
    below = params["leaf_below_r"]
    fv = mv.v
    for nd in nodes:
        if nd.get("root") or nd.get("frond"):
            continue                                 # root spurs are wood, never foliage — their
        if nd["radius"] > below:                     # thin tips would otherwise ALWAYS leaf out
            continue                                 # (childless => the density roll never applies)
        if nd["children"] and rng.random() > params["leaf_density"]:
            continue
        cx, cy, cz = _mul(nd["pos"], MP)
        r = lr * rng.uniform(0.7, 1.15)
        half = vs * 0.5
        r2 = (r + half) ** 2
        lo = [int(math.floor((c - r - vs) / vs)) * vs for c in (cx, cy, cz)]
        hi = [int(math.ceil((c + r + vs) / vs)) * vs for c in (cx, cy, cz)]
        for vx in range(lo[0], hi[0] + 1, vs):
            ddx = vx + half - cx
            for vy in range(lo[1], hi[1] + 1, vs):
                ddy = vy + half - cy
                for vz in range(lo[2], hi[2] + 1, vs):
                    ddz = vz + half - cz
                    if ddx * ddx + ddy * ddy + ddz * ddz <= r2:
                        for ix in range(vs):           # fill the leaf voxel; wood wins
                            for iy in range(vs):
                                for iz in range(vs):
                                    k = (vx + ix, vy + iy, vz + iz)
                                    if k not in fv:
                                        fv[k] = leaf


# ============================================================ presets + generate

def default_params(h, seed):
    """Base parameters for a tree of trunk-to-crown height ~h cubes. Presets tweak these."""
    return {
        "seed": seed,
        "log_mat": "Log", "leaf_mat": "Leaf",
        "envelope": "dome",
        "crown_center": (0.0, h * 0.72, 0.0),
        "canopy_r": h * 0.45, "canopy_h": h * 0.38,
        "attractors": int(h * 12),          # DETAIL dial: more attractors -> more branches
        "influence": h * 0.5, "kill": max(0.8, h * 0.06), "step": max(0.5, h * 0.04),
        "up_tropism": 0.25, "jitter": 0.35, "max_iter": 220,
        "crook": 0.25,                       # trunk/branch wander: 0 = ramrod straight, 1 = gnarled
        "trunk_r": max(0.9, h * 0.075), "r_min": 0.09, "murray_n": 2.5,
        "leaf_r": max(0.6, h * 0.045), "leaf_below_r": 0.42, "leaf_density": 0.95,
        "leaf_res": SUB_PER_CUBE,            # subcube leaves by default (perf lever: 9/3/1)
        # palm-style frond crown (off unless a preset sets `fronds` to a (min,max) count)
        "fronds": None, "frond_len": max(3.0, h * 0.42), "frond_r": 0.24, "frond_mat": "Leaf",
        # exposed root flare: the base widens (elephant-foot) and a few spurs fan out along the
        # ground while the trunk itself stays straight. Heavily randomized for a natural look.
        "root_flare": 1.6, "root_flare_h": max(2.0, h * 0.14),
        "root_count": (4, 7), "root_len": (2, 4), "root_spread": 1.15,
    }


PRESETS = {
    # ---- broadleaf domes ----
    "oak":    {"envelope": "dome", "up_tropism": 0.18, "jitter": 0.4, "crook": 0.32},
    "autumn": {"envelope": "dome", "up_tropism": 0.18, "jitter": 0.4, "crook": 0.32,
               "leaf_mat": "LeafAutumn"},
    "birch":  {"envelope": "dome", "up_tropism": 0.45, "jitter": 0.28, "crook": 0.18,
               "canopy_r_mult": 0.8, "leaf_mat": "LeafBirch", "log_mat": "LogBirch"},
    # jungle canopy tree: tall bare bole, high broad crown, heavy buttress flare
    "jungle": {"envelope": "dome", "up_tropism": 0.3, "jitter": 0.35, "crook": 0.25,
               "crown_y_frac": 0.8, "canopy_h_mult": 0.75, "canopy_r_mult": 1.05,
               "leaf_mat": "LeafJungle", "root_flare": 2.0},
    # ---- conifers (natural/organic cones — crisp whorls intentionally NOT a goal, user 2026-07-05) ----
    "pine":   {"envelope": "cone", "up_tropism": 0.62, "jitter": 0.15, "crook": 0.1,
               "canopy_r_mult": 0.62, "canopy_h_mult": 1.25, "leaf_r_mult": 0.8,
               "leaf_mat": "LeafSpruce", "log_mat": "LogSpruce"},
    "spruce": {"envelope": "cone", "up_tropism": 0.55, "jitter": 0.18, "crook": 0.12,
               "canopy_r_mult": 0.55, "canopy_h_mult": 1.35, "crown_y_frac": 0.58,
               "leaf_r_mult": 0.75, "leaf_mat": "LeafSpruce", "log_mat": "LogSpruce"},
    "fir":    {"envelope": "cone", "up_tropism": 0.7, "jitter": 0.12, "crook": 0.08,
               "canopy_r_mult": 0.42, "canopy_h_mult": 1.5, "crown_y_frac": 0.55,
               "leaf_r_mult": 0.7, "leaf_mat": "LeafSpruce", "log_mat": "LogSpruce"},
    # ---- special growth habits ----
    # acacia: umbrella slab crown on up-and-out limbs; tight leaf_below_r keeps the bole bare
    "acacia": {"envelope": "umbrella", "up_tropism": 0.45, "jitter": 0.3, "crook": 0.28,
               "crown_y_frac": 0.78, "canopy_h_mult": 0.6, "canopy_r_mult": 1.25,
               "leaf_below_r": 0.3, "leaf_density": 1.0},
    # willow: weeping curtain envelope, limbs spill over the rim and hang
    "willow": {"envelope": "weeping", "up_tropism": 0.08, "jitter": 0.3, "crook": 0.3,
               "crown_y_frac": 0.6, "canopy_r_mult": 0.9, "canopy_h_mult": 0.8,
               "leaf_density": 1.0, "leaf_below_r": 0.38},
    # palm: bare crooked trunk + frond crown; fronds ARE the foliage (leaf_mat off)
    "palm":   {"envelope": "sphere", "attractors_mult": 0.0, "crook": 0.18, "trunk_r": 0.55,
               "crown_y_frac": 0.92, "canopy_h_mult": 0.4, "leaf_mat": "",
               "fronds": (7, 11), "root_flare": 1.3},
    # dead: gnarled bare branches, no foliage at all
    "dead":   {"envelope": "dome", "up_tropism": 0.15, "jitter": 0.5, "crook": 0.5,
               "leaf_mat": "", "attractors_mult": 0.6},
    # bush: low multi-branch shrub, foliage to the ground (glow variants = leaf_mat override
    # at bake time: glow_green / glow_blue)
    "bush":   {"envelope": "dome", "up_tropism": 0.12, "jitter": 0.4, "crook": 0.3,
               "crown_y_frac": 0.5, "canopy_h_mult": 1.1, "canopy_r_mult": 1.15,
               "trunk_r": 0.3, "root_flare": 1.15, "root_count": (2, 4), "root_len": (1, 2),
               "leaf_density": 1.0, "attractors_mult": 2.0},
    # ---- giants ----
    "redwood": {"envelope": "column", "up_tropism": 0.6, "jitter": 0.15, "crook": 0.12,
                "canopy_r_mult": 0.7, "canopy_h_mult": 1.15},
    "elder_oak": {"envelope": "dome", "up_tropism": 0.15, "jitter": 0.45, "crook": 0.4},
    # enchanted-forest giant: ancient gnarled world-tree with a glowing-crack trunk (masked-emissive
    # enchanted_log material — docs/MaskedEmissiveSpec.md).
    "enchanted_oak": {"envelope": "dome", "up_tropism": 0.14, "jitter": 0.45, "crook": 0.45,
                      "log_mat": "enchanted_log"},
}
# preset keys ending in _mult scale the height-derived default of the base key (applied in build_tree)
_MULT_KEYS = {"canopy_r_mult": "canopy_r", "canopy_h_mult": "canopy_h", "leaf_r_mult": "leaf_r"}


# tiers set the perf/quality band. forest = medium density (cheap, run 50+ in a forest); hero =
# denser, finer detail (a few per scene). Trunks are round at BOTH tiers since the rasterizer gives
# thick wood a subcube surface shell (2026-07-05); the old round_trunk flag is gone.
TIERS = {
    "forest": {},
    "hero":   {"leaf_below_r": 0.5, "attractors_mult": 1.5, "leaf_r_mult2": 1.15},
}


def build_tree(preset="oak", height=15, seed=0, tier="forest", **overrides):
    """The one entry point. Returns (MicroVoxels, node_count). Archetype = preset params; `tier`
    picks the perf/quality band (forest vs hero); `height` scales the tree; `attractors` tunes branch
    density (the perf dial); any param is overridable."""
    p = default_params(height, seed)
    pr = dict(PRESETS.get(preset, {}))
    for mk, base in _MULT_KEYS.items():          # apply proportion multipliers to h-derived defaults
        if mk in pr:
            p[base] = p[base] * pr.pop(mk)
    if "crown_y_frac" in pr:                     # crown height as a fraction of tree height
        p["crown_center"] = (0.0, height * pr.pop("crown_y_frac"), 0.0)
    if "attractors_mult" in pr:                  # branch-density scale (0 = no colonization, e.g. palm)
        p["attractors"] = int(p["attractors"] * pr.pop("attractors_mult"))
    p.update(pr)
    t = dict(TIERS.get(tier, {}))
    if "attractors_mult" in t:
        p["attractors"] = int(p["attractors"] * t.pop("attractors_mult"))
    if "leaf_r_mult2" in t:
        p["leaf_r"] = p["leaf_r"] * t.pop("leaf_r_mult2")
    p.update(t)
    p.update(overrides)
    rng = random.Random(f"forge:{preset}:{height}:{seed}:{tier}:{p['attractors']}")
    nodes = grow_skeleton(rng, p)
    assign_radii(nodes, p)
    add_roots(nodes, rng, p)                     # exposed root flare on every tree
    add_fronds(nodes, rng, p)                    # palm-style frond crown (presets opt in)
    mv = MicroVoxels()
    rasterize_branches(nodes, mv, p)
    add_foliage(nodes, mv, rng, p)
    # Ground plane: y=0 IS the template floor. emit() rebases to the lowest voxel, so anything the
    # flared base cap or root spurs pushed below y=0 would float the whole trunk above the floor
    # when the template is stamped on terrain. Clip it — the trunk sits flush, roots lie on the
    # ground as tapering half-round ridges.
    mv.v = {k: m for k, m in mv.v.items() if k[1] >= 0}
    return mv, len(nodes)


def bake(preset, height, seed=0, tier="forest", name=None, out=None, display_name=None,
         outdir=None, **overrides):
    """Bake one tree to a .voxel template with a full-provenance header. Used by both the
    single-tree CLI and --batch. Returns the one-line summary string."""
    import os
    mv, nnodes = build_tree(preset, height, seed, tier, **overrides)
    lines, (nc, ns, nm), bounds = emit(mv)
    name = name or f"forge_{preset}_{height}_s{seed}"
    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    if out is None:
        out = os.path.join(outdir or os.path.join(repo, "resources", "templates"), f"{name}.voxel")
    gen = f"tree_forge.py --preset {preset} --height {height} --seed {seed} --tier {tier}"
    gen += "".join(f" --{k} {overrides[k]}" for k in sorted(overrides) if k == "attractors")
    extra = {k: v for k, v in overrides.items() if k != "attractors"}
    header = [
        "# ==========================================================",
        f"# name:         {name}",
        f"# display_name: {display_name or f'{preset.capitalize()} (forge h{height})'}",
        f"# description:  Unified space-colonization tree, {nnodes} nodes.",
        "# category:     nature", "# subcategory:  trees", "# facing:       +Z",
        f"# bounds:       {bounds[0]}W x {bounds[1]}H x {bounds[2]}D cubes",
        f"# primitives:   {nc} C + {ns} S + {nm} M = {nc + ns + nm}",
        f"# generator:    {gen}",
    ]
    if extra:                                # non-CLI param overrides, recorded for regen
        import json as _json
        header.append(f"# overrides:    {_json.dumps(extra, sort_keys=True)}")
    header += ["# ==========================================================", ""]
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(header + lines) + "\n")
    try:
        shown = os.path.relpath(out, repo)
    except ValueError:                       # Windows: --out on a different drive than the repo
        shown = out
    return (f"{name}: {nnodes} nodes -> {nc}C + {ns}S + {nm}M = {nc+ns+nm}, "
            f"bounds {bounds[0]}x{bounds[1]}x{bounds[2]} -> {shown}")


def main(argv=None):
    import argparse
    import json
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--preset", default="oak", choices=sorted(PRESETS))
    ap.add_argument("--tier", default="forest", choices=sorted(TIERS), help="forest (cheap, 50+/scene) | hero (denser)")
    ap.add_argument("--height", type=int, default=15)
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--attractors", type=int, default=None, help="detail dial (default from height)")
    ap.add_argument("--name", default=None)
    ap.add_argument("--out", default=None, help="output .voxel path (default <outdir>/<name>.voxel)")
    ap.add_argument("--batch", default=None, metavar="MANIFEST.json",
                    help="bake a library: [{name, preset, height, seed, tier, ...overrides}, ...] "
                         "(see tools/forge_library.json)")
    ap.add_argument("--outdir", default=None, help="output directory (default resources/templates)")
    args = ap.parse_args(argv)

    if args.batch:
        with open(args.batch, encoding="utf-8") as f:
            entries = json.load(f)
        for e in entries:
            print(bake(outdir=args.outdir, **e))
        print(f"batch: {len(entries)} trees baked from {args.batch}")
        return 0

    ov = {}
    if args.attractors is not None:
        ov["attractors"] = args.attractors
    print(bake(args.preset, args.height, args.seed, args.tier,
               name=args.name, out=args.out, outdir=args.outdir, **ov))
    return 0


if __name__ == "__main__":
    import sys
    sys.exit(main())
