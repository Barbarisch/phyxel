"""Deterministic validation gates on the compiled voxel rig.

Ported from anyCreature engine/core/checks.js and adapted from mesh space to
voxel space; plus Phyxel-only gates (box budget, anim_lint, per-box color
quirk). Severities: BLOCK (refuse the build) / WARN (author judgement).

Deliberate deviations, documented:
* `proportion` (the 50:50 segment-rhythm gate) is a WARN here, not a BLOCK —
  the extracted 0.923 adjacent-ratio threshold blocks anyCreature's own
  shipped wolf example (front-leg segments 0.355/0.344 = 0.968), so the
  faithful-threshold port cannot be a hard gate until re-derived
* per-bone voxel disconnection is a WARN (curve parts legitimately hover
  a hair off their host at voxel resolution)
"""
from __future__ import annotations

import math
from collections import namedtuple

import anim_lint
from finalize_quadruped import _fk_fn
from gen_quadruped_walk import q_rot

Finding = namedtuple("Finding", "severity rule message")

BOX_BUDGET = 262144 // 20  # RenderCoordinator capacity / CharacterInstanceBudgetTest


# ---------------------------------------------------------------------------
# geometry helpers
# ---------------------------------------------------------------------------

def _convex_hull_xz(points):
    pts = sorted(set((round(x, 6), round(z, 6)) for x, z in points))
    if len(pts) <= 2:
        return pts

    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])

    lower, upper = [], []
    for p in pts:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], p) <= 0:
            lower.pop()
        lower.append(p)
    for p in reversed(pts):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], p) <= 0:
            upper.pop()
        upper.append(p)
    return lower[:-1] + upper[:-1]


def _inside_hull(p, hull):
    if len(hull) < 3:
        return False
    n = len(hull)
    for i in range(n):
        a, b = hull[i], hull[(i + 1) % n]
        if (b[0] - a[0]) * (p[1] - a[1]) - (b[1] - a[1]) * (p[0] - a[0]) < 0:
            return False
    return True


def _bone_points(compiled):
    """bone name -> list of representative world points (box centers)."""
    world = compiled.bind_world_positions()
    pts = {}
    for bx in compiled.af.boxes:
        name = compiled.af.bones[bx.bone_id].name
        bw = world[name]
        pts.setdefault(name, []).append(
            (bw[0] + bx.center[0], bw[1] + bx.center[1], bw[2] + bx.center[2]))
    return pts


def _min_dist(pa, pb):
    best = 1e18
    for a in pa:
        for b in pb:
            d = ((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2)
            if d < best:
                best = d
    return math.sqrt(best)


def _downsample(points, cap=80):
    if len(points) <= cap:
        return points
    stride = len(points) // cap + 1
    return points[::stride]


# ---------------------------------------------------------------------------
# rules
# ---------------------------------------------------------------------------

def _rule_budget(compiled, out):
    n = len(compiled.af.boxes)
    if n > BOX_BUDGET:
        out.append(Finding("BLOCK", "box_budget",
                           f"{n} boxes exceeds the per-rig budget of {BOX_BUDGET} "
                           "(kCharacterInstanceCapacity / 20 dense creatures)"))
    elif n > 4667:
        out.append(Finding("WARN", "box_budget",
                           f"{n} boxes exceeds the densest shipped rig (4667)"))


def _rule_ownership(compiled, out):
    """A bone must own geometry OR be an embedded articulation pivot (its
    joint sits inside mass claimed by the host — first-writer-wins absorbs
    attached-chain roots into the torso, which is fine)."""
    owned = {bx.bone_id for bx in compiled.af.boxes}
    grid = compiled.grid
    for b in compiled.af.bones:
        if b.name == "ground_ref":
            continue
        if b.id in owned:
            continue
        jp = compiled.sk.world.get(b.name)
        embedded = False
        if jp is not None:
            k = grid.key_of(jp)
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    for dz in (-1, 0, 1):
                        if (k[0] + dx, k[1] + dy, k[2] + dz) in grid.cells:
                            embedded = True
        if not embedded:
            out.append(Finding("BLOCK", "voxel_integrity",
                               f"bone '{b.name}' owns no geometry and is not "
                               "embedded in any"))


def _rule_connectivity(compiled, out):
    by_bone = {}
    for key, cell in compiled.grid.cells.items():
        by_bone.setdefault(cell[0], set()).add(key)
    for bone in sorted(by_bone):
        cells = by_bone[bone]
        seen = set()
        start = min(cells)
        stack = [start]
        while stack:
            c = stack.pop()
            if c in seen:
                continue
            seen.add(c)
            x, y, z = c
            for n in ((x + 1, y, z), (x - 1, y, z), (x, y + 1, z),
                      (x, y - 1, z), (x, y, z + 1), (x, y, z - 1)):
                if n in cells and n not in seen:
                    stack.append(n)
        if len(seen) != len(cells):
            out.append(Finding("WARN", "voxel_integrity",
                               f"bone '{bone}' voxels split into disconnected "
                               f"fragments ({len(cells) - len(seen)} cells adrift)"))


def _rule_balance(compiled, out):
    """Bind XZ mass centroid must sit inside the support polygon of the
    lowest 8% of the model (checks.js `balance`)."""
    pts = []
    world = compiled.bind_world_positions()
    for bx in compiled.af.boxes:
        name = compiled.af.bones[bx.bone_id].name
        bw = world[name]
        cx, cy, cz = (bw[0] + bx.center[0], bw[1] + bx.center[1],
                      bw[2] + bx.center[2])
        pts.append((cx, cy, cz, bx.size[0] * bx.size[1] * bx.size[2],
                    bx.size[0], bx.size[1], bx.size[2]))
    if not pts:
        return
    ys_lo = [p[1] - p[5] / 2 for p in pts]
    y0, y1 = min(ys_lo), max(p[1] + p[5] / 2 for p in pts)
    cut = y0 + 0.08 * (y1 - y0)
    support = []
    for cx, cy, cz, _, sx, sy, sz in pts:
        if cy - sy / 2 <= cut:
            support.append((cx - sx / 2, cz - sz / 2))
            support.append((cx - sx / 2, cz + sz / 2))
            support.append((cx + sx / 2, cz - sz / 2))
            support.append((cx + sx / 2, cz + sz / 2))
    total_m = sum(p[3] for p in pts) or 1.0
    centroid = (sum(p[0] * p[3] for p in pts) / total_m,
                sum(p[2] * p[3] for p in pts) / total_m)
    hull = _convex_hull_xz(support)
    if not _inside_hull(centroid, hull):
        out.append(Finding("BLOCK", "balance",
                           f"XZ mass centroid {tuple(round(c, 3) for c in centroid)} "
                           f"falls outside the support polygon of the lowest 8%"))


def _rule_limb_clearance(compiled, out):
    """Mirrored-limb centreline gap 2*min|x| must be >= 0.03 * model height.
    NOTE: uses bind_world_positions() (post-target-height scale), never
    sk.world (native spec scale) — mixing the two spaces was a live bug."""
    height = _model_height(compiled)
    sk = compiled.sk
    world = compiled.bind_world_positions()
    for lchain, rchain in sk.mirror_chain.items():
        xs = [abs(world[jn][0]) for jn in sk.chains[lchain][1:]]
        if not xs:
            continue
        gap = 2.0 * min(xs)
        if gap < 0.03 * height:
            out.append(Finding("BLOCK", "limb_clearance",
                               f"mirrored chain '{lchain}' centreline gap "
                               f"{gap:.3f} < {0.03 * height:.3f} (limbs will fuse)"))


def _model_height(compiled):
    ys = []
    world = compiled.bind_world_positions()
    for bx in compiled.af.boxes:
        bw = world[compiled.af.bones[bx.bone_id].name]
        ys.append(bw[1] + bx.center[1] - bx.size[1] / 2)
        ys.append(bw[1] + bx.center[1] + bx.size[1] / 2)
    return (max(ys) - min(ys)) if ys else 1.0


def _rule_root_containment(compiled, out):
    """The root joint of an attached, volumed chain must sit in/next to the
    host geometry (voxel-space stand-in for the 80% root-ring rule)."""
    spec = compiled.spec
    sk = compiled.sk
    attach = spec.get("attach", {})
    vol_chains = {v["chain"] for v in spec.get("volumes", [])}
    bone_pts = _bone_points(compiled)
    world = compiled.bind_world_positions()
    tol = 2.5 * compiled.options.voxel_size
    for cn in vol_chains:
        if cn not in attach:
            continue
        root_joint = spec["chains"][cn][0]
        rp = world[root_joint]
        chain_bones = set(sk.chains[cn])
        others = [p for name, pl in bone_pts.items()
                  if name not in chain_bones for p in _downsample(pl)]
        if not others:
            continue
        d = _min_dist([rp], others)
        if d > tol + 0.015 * _model_height(compiled):
            out.append(Finding("BLOCK", "root_containment",
                               f"chain '{cn}' root joint '{root_joint}' floats "
                               f"{d:.3f} from the host geometry"))


def _rule_part_attachment(compiled, out):
    height = _model_height(compiled)
    sk = compiled.sk
    bone_pts = _bone_points(compiled)
    world = compiled.bind_world_positions()
    for part in compiled.spec.get("parts", []):
        host = part.get("host")
        if host is None or host not in world:
            continue
        hp = world[host]
        # host bone must have geometry near its own joint for the part to sit on
        near = [p for name, pl in bone_pts.items() for p in _downsample(pl)]
        if not near:
            continue
        d = _min_dist([hp], near)
        if d > 0.015 * height + 3.0 * compiled.options.voxel_size:
            out.append(Finding("BLOCK", "part_attachment",
                               f"part '{part['type']}' host '{host}' is "
                               f"{d:.3f} from any geometry"))


def _rule_mirror_distortion(compiled, out):
    sk = compiled.sk
    bone_pts = _bone_points(compiled)
    for lchain, rchain in sk.mirror_chain.items():
        for names, other in ((sk.chains[lchain], sk.chains[rchain]),):
            lpts = [p for jn in names for p in bone_pts.get(jn, [])]
            rpts = [p for jn in other for p in bone_pts.get(jn, [])]
            if not lpts or not rpts:
                continue
            for axis in range(3):
                le = max(p[axis] for p in lpts) - min(p[axis] for p in lpts)
                re = max(p[axis] for p in rpts) - min(p[axis] for p in rpts)
                if le < 1e-6 and re < 1e-6:
                    continue
                ratio = (re / le) if le > 1e-6 else 99.0
                if ratio < 0.70 or ratio > 1.30:
                    out.append(Finding("BLOCK", "mirror_distortion",
                                       f"chain '{lchain}' mirror extent ratio "
                                       f"{ratio:.2f} on axis {axis} outside 0.70-1.30"))
                elif ratio < 0.88 or ratio > 1.12:
                    out.append(Finding("WARN", "mirror_distortion",
                                       f"chain '{lchain}' mirror extent ratio "
                                       f"{ratio:.2f} on axis {axis}"))


def _rule_touch(compiled, out):
    height = _model_height(compiled)
    sk = compiled.sk
    bone_pts = _bone_points(compiled)
    for pair in compiled.spec.get("touch", []):
        ca, cb = pair
        pa = [p for jn in sk.chains.get(ca, []) for p in bone_pts.get(jn, [])]
        pb = [p for jn in sk.chains.get(cb, []) for p in bone_pts.get(jn, [])]
        if not pa or not pb:
            out.append(Finding("BLOCK", "touch",
                               f"touch pair {pair} has a side with no geometry"))
            continue
        d = _min_dist(_downsample(pa), _downsample(pb))
        if d > 0.01 * height + 2.0 * compiled.options.voxel_size:
            out.append(Finding("BLOCK", "touch",
                               f"declared touch {pair} gap {d:.3f}"))


def _rule_proportion(compiled, out):
    """WARN-only port of the 50:50 segment-rhythm gate (see module doc)."""
    sk = compiled.sk
    style_heavy = compiled.spec.get("style") == "heavy"
    if style_heavy:
        return
    mirrored = set(sk.mirror_chain.values())
    for cn, names in sk.chains.items():
        if cn in mirrored or len(names) < 3:
            continue
        segs = []
        for i in range(1, len(names)):
            a, b = sk.world[names[i - 1]], sk.world[names[i]]
            segs.append(math.dist(a, b))
        mean = sum(segs) / len(segs)
        for i in range(1, len(segs)):
            s0, s1 = segs[i - 1], segs[i]
            if s0 < 0.5 * mean or s1 < 0.5 * mean:
                continue
            ratio = min(s0, s1) / (max(s0, s1) or 1.0)
            if ratio > 0.923:
                out.append(Finding("WARN", "proportion",
                                   f"chain '{cn}' segments {i - 1}/{i} are "
                                   f"{ratio:.2f} equal (50:50 rhythm reads static)"))


def _rule_anim_integrity(compiled, out):
    """FK-pose box centers at 5 phases of each clip; a parent-child gap that
    grows by more than 2 voxels over bind means the joint tears."""
    af = compiled.af
    vs = compiled.options.voxel_size
    by_bone_local = {}
    for bx in af.boxes:
        by_bone_local.setdefault(bx.bone_id, []).append(bx.center)
    for bid in by_bone_local:
        by_bone_local[bid] = _downsample(sorted(by_bone_local[bid]), 60)
    pairs = [(b.id, b.parent_id) for b in af.bones
             if b.parent_id >= 0 and b.id in by_bone_local
             and b.parent_id in by_bone_local]

    def posed_gap(fk_t):
        gp, gr = fk_t
        worst = None
        for child, parent in pairs:
            pc = [tuple(gp[child][k] + q_rot(gr[child], c)[k] for k in range(3))
                  for c in by_bone_local[child]]
            pp = [tuple(gp[parent][k] + q_rot(gr[parent], c)[k] for k in range(3))
                  for c in by_bone_local[parent]]
            d = _min_dist(pc, pp)
            if worst is None or d > worst[0]:
                worst = (d, child, parent)
        return worst

    for clip in af.clips:
        fk, dur = _fk_fn(af, clip.name)
        if dur <= 0:
            continue
        bind = {}
        for child, parent in pairs:
            gp0, gr0 = fk(-1e9)  # clamps to first keys ~ near-bind
            pc = [tuple(gp0[child][k] + q_rot(gr0[child], c)[k] for k in range(3))
                  for c in by_bone_local[child]]
            pp = [tuple(gp0[parent][k] + q_rot(gr0[parent], c)[k] for k in range(3))
                  for c in by_bone_local[parent]]
            bind[(child, parent)] = _min_dist(pc, pp)
        for phase in (0.0, 0.2, 0.4, 0.6, 0.8):
            gp, gr = fk(phase * dur)
            for child, parent in pairs:
                pc = [tuple(gp[child][k] + q_rot(gr[child], c)[k] for k in range(3))
                      for c in by_bone_local[child]]
                pp = [tuple(gp[parent][k] + q_rot(gr[parent], c)[k] for k in range(3))
                      for c in by_bone_local[parent]]
                d = _min_dist(pc, pp)
                if d > bind[(child, parent)] + 2.0 * vs:
                    out.append(Finding(
                        "BLOCK", "anim_integrity",
                        f"clip '{clip.name}' t={phase:.1f}: joint "
                        f"'{af.bones[child].name}' tears "
                        f"{d - bind[(child, parent)]:.3f} from its parent"))
                    break


def _rule_attack_reach(compiled, out):
    attack = compiled.af.clip("attack")
    if attack is None:
        return
    af = compiled.af
    world = compiled.bind_world_positions()
    bind_max_z = max((world[af.bones[bx.bone_id].name][2] + bx.center[2]
                      for bx in af.boxes), default=0.0)
    span = _model_height(compiled)
    fk, dur = _fk_fn(af, "attack")
    reach = -1e9
    for i in range(11):
        gp, gr = fk(i / 10 * dur)
        for bx in af.boxes:
            p = q_rot(gr[bx.bone_id], bx.center)
            reach = max(reach, gp[bx.bone_id][2] + p[2])
    if reach < bind_max_z + 0.15 * span:
        out.append(Finding("BLOCK", "attack_reach",
                           f"attack clip reaches {reach - bind_max_z:.3f} past "
                           f"bind front; needs >= {0.15 * span:.3f}"))


def _rule_required_clips(compiled, out):
    """A combat creature without a resolvable attack/death clip fails
    SILENTLY WRONG in-engine (the FSM stays in Attack on a stale clip and
    damage fires at 0.4 of idle's duration) — refuse at build time."""
    if not compiled.spec.get("combat"):
        return
    clips = {c.name for c in compiled.af.clips}
    missing = {"idle", "walk", "attack", "death"} - clips
    if missing:
        out.append(Finding("BLOCK", "required_clips",
                           f"combat creature missing clips: {sorted(missing)}"))


def _rule_death_pose(compiled, out):
    """The engine plays a Death clip once and freezes the LAST frame — a
    death clip must end with the body down. BLOCK unless the box-mass
    centroid height at the clip's end drops to <= 65% of bind height."""
    death = compiled.af.clip("death")
    if death is None:
        return
    af = compiled.af

    def centroid_y(fk_t):
        gp, gr = fk_t
        total_w = 0.0
        acc = 0.0
        for bx in af.boxes:
            w = bx.size[0] * bx.size[1] * bx.size[2]
            c = q_rot(gr[bx.bone_id], bx.center)
            acc += (gp[bx.bone_id][1] + c[1]) * w
            total_w += w
        return acc / (total_w or 1.0)

    def min_y(fk_t):
        gp, gr = fk_t
        lo = 1e18
        for bx in af.boxes:
            c = q_rot(gr[bx.bone_id], bx.center)
            lo = min(lo, gp[bx.bone_id][1] + c[1] - bx.size[1] / 2)
        return lo

    fk, dur = _fk_fn(af, "death")
    if dur <= 0:
        return
    bind = fk(-1e9)
    final = fk(dur)
    ground = min_y(bind)
    bind_h = centroid_y(bind) - ground
    final_h = centroid_y(final) - ground
    if bind_h > 1e-6 and final_h > 0.65 * bind_h:
        out.append(Finding(
            "BLOCK", "death_pose",
            f"death clip ends with centroid at {final_h / bind_h:.0%} of bind "
            "height — the engine freezes the last frame, so the creature must "
            "end DOWN (<= 65%)"))


def _rule_lint(compiled, out):
    """anim_lint findings are (severity, message) tuples. Loop-closure is
    only asserted for clips the spec marks loop:true — attack/death clips
    legitimately end in a different pose."""
    spec_anims = compiled.spec.get("animations", {})
    aliases = {"move": "walk"}
    loop_clips = set()
    for name, a in spec_anims.items():
        if a.get("loop"):
            loop_clips.add(aliases.get(name, name))
    for clip in compiled.af.clips:
        looping = clip.name in loop_clips
        for sev_str, msg in anim_lint.lint_clip(compiled.af, clip,
                                                looping=looping):
            sev = "BLOCK" if sev_str == "ERROR" else "WARN"
            out.append(Finding(sev, "anim_lint", f"{clip.name}: {msg}"))


# ---------------------------------------------------------------------------
# entry
# ---------------------------------------------------------------------------

def run(compiled) -> list:
    out = []
    for w in compiled.warnings:
        out.append(Finding("WARN", "compile", w))
    _rule_budget(compiled, out)
    _rule_ownership(compiled, out)
    _rule_connectivity(compiled, out)
    _rule_balance(compiled, out)
    _rule_limb_clearance(compiled, out)
    _rule_root_containment(compiled, out)
    _rule_part_attachment(compiled, out)
    _rule_mirror_distortion(compiled, out)
    _rule_touch(compiled, out)
    _rule_proportion(compiled, out)
    _rule_anim_integrity(compiled, out)
    _rule_attack_reach(compiled, out)
    _rule_required_clips(compiled, out)
    _rule_death_pose(compiled, out)
    _rule_lint(compiled, out)
    return out
