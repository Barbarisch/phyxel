"""binder.py — fit OUR skeleton to a voxelized model and bind every voxel to a bone.

Roadmap R2 decision 3 (docs/CharacterAnimationRoadmap.md): Meshy's auto-rigger is humanoid-only
and its skeleton is not ours, so the rig always comes from our side — the humanoid skeleton for
bipeds, a forge species skeleton for everything else — and its clips play unchanged.

  1. Bind-pose FK of the target rig. Box offsets live in BONE-LOCAL frames: the engine places a
     part at globalTransform[bone] * offset (AnimatedVoxelCharacter.cpp, part build).
  2. Fit: a global map p -> s*p + t from the rig's own box cloud to the voxel cloud.
       creature: per-axis s (bbox to bbox). Forge rigs have identity bind rotations, so a
                 non-uniform map keeps every local frame valid.
       biped:    uniform s from height (the T-pose arm span must not squash the shoulders), then
                 each arm chain is rotated about its shoulder onto the mesh's arm direction and
                 stretched to the mesh's reach. Deviation from the gate, recorded in the roadmap:
                 a geometric fit until real Meshy rigged output exists to read landmarks from.
  3. Legs: every leg chain (foot leaf, walked up while the parent has one child) moves in x/z
     onto the centroid of the low voxels on its side and half, so legs land in the mesh's legs.
  4. Assign: each voxel goes to the bone segment (joint -> each child joint) nearest RELATIVE TO
     that bone's allowance (below), never to a bone on the opposite side (L*/Left* bones are +X
     because the model faces +Z).
  5. Clips: a position key's offset from its bone's bind position is scaled by s and applied to
     the FITTED bind position (valid because s is uniform or the parents' bind rotations are
     identity). Rotation keys unchanged.

Metrics (the import gate's pass bar):
  within_frac        voxels within max(0.15 * extent, 1.25 * the species rig's own envelope for
                     that bone * fit scale) of their assigned segment (bar >= 0.98). The gate's flat
                     0.15 * height bar failed the shipped rigs themselves (bone_envelopes); the flat
                     number is still reported as within_frac_flat.
  side_violations    voxels with |x| > pitch on an opposite-side bone (bar 0)
  feet_ground_err_u  worst leg: lowest assigned voxel's bottom face above the ground (bar 0.05 extent)
Deterministic: no RNG; ties break by segment order (bone id).
"""
from __future__ import annotations

import copy
import re
import sys
from pathlib import Path

import numpy as np
from scipy.spatial.transform import Rotation as R

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "anim_pipeline"))
import anim_format as af  # noqa: E402

HELPER_BONES = {"ground_ref"}
# AnimatedVoxelCharacter.cpp (part build) drops every box on a bone whose lower-cased name contains
# one of these, so a voxel bound there is invisible. Kept in sync by a test that reads the engine.
ENGINE_SKIPPED = ("thumb", "index", "middle", "ring", "pinky", "eye", "toe", "end")


def side_of(name: str) -> int:
    base = name.split(":")[-1]
    if re.match(r"^L[A-Z]", base) or "left" in base.lower():
        return 1
    if re.match(r"^R[A-Z]", base) or "right" in base.lower():
        return -1
    return 0


def _local(b) -> np.ndarray:
    m = np.eye(4)
    m[:3, :3] = R.from_quat(b.rot).as_matrix() * np.asarray(b.scale, dtype=float)
    m[:3, 3] = b.pos
    return m


def fk(anim) -> dict:
    g = {}
    for b in anim.bones:          # parents precede children in every .anim we ship
        g[b.id] = _local(b) if b.parent_id < 0 else g[b.parent_id] @ _local(b)
    return g


def _children(anim) -> dict:
    ch = {b.id: [] for b in anim.bones}
    for b in anim.bones:
        if b.parent_id >= 0:
            ch[b.parent_id].append(b.id)
    return ch


def _subtree(ch, root) -> list:
    out, stack = [], [root]
    while stack:
        n = stack.pop()
        out.append(n)
        stack.extend(ch[n])
    return sorted(out)


def _leg_roots(anim, g, ch, height) -> list:
    """(leg root, foot leaf) pairs: leaves near the ground, walked up while the parent has 1 child."""
    legs = set()
    for b in anim.bones:
        if ch[b.id] or b.name in HELPER_BONES or g[b.id][1, 3] > 0.1 * height:
            continue
        if any("tail" in anim.bones[a].name.lower() for a in _path_to_root(anim, b.id)):
            continue    # a hanging tail tip is not a foot (panther, 2026-10-02)
        n = b.id
        while anim.bones[n].parent_id >= 0 and len(ch[anim.bones[n].parent_id]) == 1:
            n = anim.bones[n].parent_id
        if anim.bones[n].parent_id >= 0:
            legs.add((n, b.id))
    return sorted(legs)


def _stance_speed(anim, clip, feet, height, hz=60.0):
    """Body speed the planted feet imply for an in-place clip: the magnitude of the mean XZ
    velocity of feet within 2 % of height of their own lowest point — the MotionOracle /
    anim_lint stance estimate, in Python, on the binder's own feet."""
    import anim_lint
    n = max(2, int(clip.duration * hz) + 1)
    pos = [anim_lint.pose_world_positions(anim, clip, clip.duration * i / (n - 1)) for i in range(n)]
    v = []
    for f in feet:
        ys = np.array([p[f][1] for p in pos])
        xz = np.array([(p[f][0], p[f][2]) for p in pos])
        planted = ys - ys.min() < 0.02 * height
        for i in range(1, n):
            if planted[i] and planted[i - 1]:
                v.append((xz[i] - xz[i - 1]) * hz)
    # |mean stance-foot XZ velocity| — anim_lint.foot_slide_metrics' estimate (direction-free:
    # a sign-only Z estimate read 0 for clips whose feet travel the other way)
    return float(np.linalg.norm(np.mean(v, axis=0))) if v else 0.0


def _ground_contacts(low_pts, pitch):
    """Connected clusters (26-neighbourhood on the voxel grid) of the near-ground voxels: one per
    foot that touches down separately."""
    from scipy import ndimage
    if len(low_pts) == 0:
        return []
    idx = np.round((low_pts - low_pts.min(axis=0)) / pitch).astype(int)
    grid = np.zeros(idx.max(axis=0) + 1, dtype=bool)
    grid[idx[:, 0], idx[:, 1], idx[:, 2]] = True
    lab, n = ndimage.label(grid, structure=np.ones((3, 3, 3)))
    which = lab[idx[:, 0], idx[:, 1], idx[:, 2]]
    return [low_pts[which == k] for k in range(1, n + 1)]


def _geo_sides(anim, g, height) -> dict:
    """Left/right from WHERE a bone is, not what it is called: forge spiders name sides `_L`/`_R`
    and put `_L` at -X, quadrupeds put `L*` at +X (2026-10-02). Midline bones are 0."""
    out = {}
    for b in anim.bones:
        x = g[b.id][0, 3]
        out[b.id] = 0 if abs(x) < 0.02 * height else (1 if x > 0 else -1)
    return out


def _rebuild_locals(anim, g):
    """Write bone pos/rot back from edited global transforms."""
    for b in anim.bones:
        loc = g[b.id] if b.parent_id < 0 else np.linalg.inv(g[b.parent_id]) @ g[b.id]
        rot = loc[:3, :3] / np.asarray(b.scale, dtype=float)
        b.pos = tuple(float(round(x, 6)) for x in loc[:3, 3])
        q = R.from_matrix(rot).as_quat()
        if q[3] < 0:
            q = -q
        b.rot = tuple(float(round(x, 6)) for x in q)


def _rotate_subtree(g, ids, pivot, rot: R):
    a = np.eye(4)
    a[:3, :3] = rot.as_matrix()
    a[:3, 3] = pivot - rot.as_matrix() @ pivot
    for i in ids:
        g[i] = a @ g[i]


def _segments(anim, g, ch):
    """(bone id, a, b) per bone -> child segment; leaves give a point segment."""
    segs = []
    for b in anim.bones:
        if b.name in HELPER_BONES or any(k in b.name.lower() for k in ENGINE_SKIPPED):
            continue    # the engine never renders boxes on these bones
        p = g[b.id][:3, 3]
        kids = [c for c in ch[b.id] if anim.bones[c].name not in HELPER_BONES]
        for c in kids or [b.id]:
            segs.append((b.id, p, g[c][:3, 3]))
    return segs


def _seg_dist(pts, a, b):
    ab = b - a
    den = float(ab @ ab)
    t = np.zeros(len(pts)) if den < 1e-12 else np.clip((pts - a) @ ab / den, 0.0, 1.0)
    return np.linalg.norm(pts - (a + t[:, None] * ab), axis=1)


def _fit_biped_spine(anim, g, ch, pts, height, pitch) -> int:
    """Biped: each spine joint (hips -> head) goes to the torso centre (median x/z of the voxels
    within the central 30 % of the width) at its own height, and everything hanging from it but
    not on the spine (arms, legs, the head's leaves) moves with it. The gnoll's hunched back sat
    0.33 u in front of an upright humanoid spine (2026-10-02)."""
    names = {b.name.split(":")[-1]: b.id for b in anim.bones}
    head = names.get("HeadTop_End", names.get("Head"))
    if head is None:
        return 0
    path = _path_to_root(anim, head)
    hips = names.get("Hips", path[0])
    path = path[path.index(hips):] if hips in path else path
    delta = {}
    for i in path:
        y = g[i][1, 3]
        sl = pts[(np.abs(pts[:, 1] - y) < 1.5 * pitch) & (np.abs(pts[:, 0]) < 0.15 * height)]
        if len(sl) < 3:
            continue
        new = np.array([np.median(sl[:, 0]), y, np.median(sl[:, 2])])
        delta[i] = new - g[i][:3, 3]
    if not delta:
        return 0
    pset = set(path)
    # legs are NOT carried along: the ground-contact leg fit places them on the mesh feet from
    # their own reference (carrying them shifted the gnoll's legs twice, 2026-10-02)
    leg_set = set()
    for root, _ in _leg_roots(anim, g, ch, height):
        leg_set.update(_subtree(ch, root))
    moved_by = {}
    for b in anim.bones:            # parents precede children
        if b.id in delta:
            moved_by[b.id] = delta[b.id]
        elif b.parent_id >= 0 and b.parent_id in moved_by and b.id not in pset and b.id not in leg_set:
            moved_by[b.id] = moved_by[b.parent_id]
    for i, d in moved_by.items():
        g[i][:3, 3] += d
    return len(delta)


def _fit_arms(anim, g, ch, pts, height, pitch):
    """Biped: rotate each arm chain about its shoulder onto the mesh arm, stretch to its reach."""
    names = {b.name.split(":")[-1]: b.id for b in anim.bones}
    report = {}
    for side, arm, hand in ((1, "LeftArm", "LeftHand"), (-1, "RightArm", "RightHand")):
        if arm not in names or hand not in names:
            continue
        ia, ih = names[arm], names[hand]
        sh = g[ia][:3, 3].copy()
        # arm voxels: outboard of the shoulder joint, above the hips
        sel = pts[(side * pts[:, 0] > side * sh[0] + 2 * pitch) & (pts[:, 1] > 0.45 * height)]
        if len(sel) < 4:
            continue
        rel = sel - sh
        reach_mesh = float(np.linalg.norm(rel, axis=1).max())
        d_mesh = rel.mean(axis=0)
        d_mesh /= np.linalg.norm(d_mesh)
        d_rig = g[ih][:3, 3] - sh
        reach_rig = float(np.linalg.norm(d_rig))
        rot, _ = R.align_vectors([d_mesh], [d_rig / reach_rig])
        sub = _subtree(ch, ia)
        _rotate_subtree(g, sub, sh, rot)
        # the wrist sits ~80 % of the way along the arm (hand + fingers beyond it)
        k = reach_mesh * 0.8 / reach_rig
        if 0.5 < k < 2.0:
            for i in sub:
                if i != ia:
                    g[i][:3, 3] = sh + (g[i][:3, 3] - sh) * k
        # per-segment refinement (orc 2026-10-02: elbows bent, forearms forward): the upper arm
        # aims at the mesh's elbow region, then the forearm aims at the far end of the arm
        fore = next((c for c in ch[ia] if c in sub), None)
        bent = 0.0
        if fore is not None:
            dist = np.linalg.norm(rel, axis=1)
            mid = sel[(dist >= 0.35 * reach_mesh) & (dist <= 0.55 * reach_mesh)]
            far = sel[dist >= 0.85 * reach_mesh]
            if len(mid) >= 2 and len(far) >= 2:
                r1, _ = R.align_vectors([mid.mean(axis=0) - sh], [g[fore][:3, 3] - sh])
                _rotate_subtree(g, sub, sh, r1)
                el = g[fore][:3, 3].copy()
                r2, _ = R.align_vectors([far.mean(axis=0) - el], [g[ih][:3, 3] - el])
                _rotate_subtree(g, _subtree(ch, fore), el, r2)
                bent = float(np.degrees(r2.magnitude()))
        report[arm] = {"deg": round(float(np.degrees(rot.magnitude())), 1), "stretch": round(k, 3),
                       "elbow_deg": round(bent, 1)}
    return report


def bone_envelopes(anim) -> dict:
    """Per bone: the farthest of the rig's OWN boxes from that bone's segments (rig units).
    Control measured 2026-10-01: a flat 0.15 x body-height bar fails the shipped rigs themselves
    (forge_bear 58 %, forge_serpent 8 %, hand-authored humanoid 95.7 %) because it measures body
    thickness, not binding. The species' own envelope is the honest reference."""
    g = fk(anim)
    ch = _children(anim)
    env = {}
    segs = _segments(anim, g, ch)
    by_bone = {}
    for bid, a, b in segs:
        by_bone.setdefault(bid, []).append((a, b))
    if not anim.boxes:
        return env
    w = np.array([(g[x.bone_id] @ np.r_[x.center, 1.0])[:3] for x in anim.boxes])
    own = np.array([x.bone_id for x in anim.boxes])
    d_all = np.stack([_seg_dist(w, a, b) for _, a, b in segs], axis=1)
    seg_bone = np.array([bid for bid, _, _ in segs])
    nearest = d_all.min(axis=1)
    for bid in np.unique(own):
        cols = seg_bone == bid
        if not cols.any():
            continue
        m = own == bid
        d_own = d_all[m][:, cols].min(axis=1)
        # ignore boxes the source rig itself binds anomalously (much nearer another bone): the
        # shipped humanoid binds 48 pelvis boxes to LeftHandPinky4 / RightToe_End (logged in
        # docs/StructurePipelineGaps.md 2026-10-01); they would make those bones voxel magnets
        ok = d_own <= 2.0 * nearest[m] + 0.05
        if ok.any():
            env[int(bid)] = float(d_own[ok].max())
    return env


def _path_to_root(anim, i) -> list:
    out = []
    while i >= 0:
        out.append(i)
        i = anim.bones[i].parent_id
    return out[::-1]


def _fit_axis(anim, g, ch, pts, pitch, legs):
    """Creature: put the root->head and root->tail joints at the centre of the body's cross-section
    (the gate's 'each spec segment scaled to its measured landmark' for the body axis). Leg
    voxels (below the leg roots' mid height) are excluded so the slab measures the body, not legs."""
    leaves = [b.id for b in anim.bones if not ch[b.id] and b.name not in HELPER_BONES]
    leg_bones = set()
    for root, _ in legs:
        leg_bones.update(_subtree(ch, root))
    jaw_bones = set()
    for b in anim.bones:
        if b.name.lower() == "jaw":
            jaw_bones.update(_subtree(ch, b.id))
    axis_leaves = [i for i in leaves if i not in leg_bones and i not in jaw_bones]
    if not axis_leaves:
        return 0
    head = max(axis_leaves, key=lambda i: (g[i][2, 3], -i))
    tail = min(axis_leaves, key=lambda i: (g[i][2, 3], i))
    leg_top = np.mean([g[r][1, 3] for r, _ in legs]) if legs else 0.0
    body = pts[pts[:, 1] > 0.5 * leg_top]
    moved = 0
    path = set(_path_to_root(anim, head)) | set(_path_to_root(anim, tail))
    delta = {}
    for i in sorted(path):
        slab = body[np.abs(body[:, 2] - g[i][2, 3]) <= 1.5 * pitch]
        if len(slab) < 3:
            continue
        old = g[i][:3, 3].copy()
        g[i][1, 3] = float((slab[:, 1].min() + slab[:, 1].max()) / 2)
        g[i][0, 3] = float((slab[:, 0].min() + slab[:, 0].max()) / 2)
        delta[i] = g[i][:3, 3] - old
        moved += 1
    # side branches (a jaw, ears, horns) move with the axis joint they hang from; legs keep their
    # own fit. Measured 2026-10-01: without this the owlbear's Jaw sat above its skull.
    for b in anim.bones:
        if b.id in path or b.id in leg_bones or b.name in HELPER_BONES:
            continue
        a = b.parent_id
        while a >= 0 and a not in path:
            a = anim.bones[a].parent_id
        if a in delta:
            g[b.id][:3, 3] += delta[a]
    return moved


JAW_HINGE_FRAC = 0.35   # the hinge sits 35 % up the height of the head in front of it


def _fit_jaw(anim, g, ch, pts, bone_of, pitch) -> bool:
    """Place Jaw (hinge) and its tip on the MESH's head: the spec's jaw offset is relative to the
    species' own head, and the owlbear's beak sat entirely above it (0 voxels split). Hinge keeps
    its fitted z, at JAW_HINGE_FRAC of the height of the head in front of it; the tip goes to the middle of
    the frontmost head slab. Uses the first-pass head assignment to find the head's voxels."""
    jaw = next((b.id for b in anim.bones if b.name.lower() == "jaw"), None)
    if jaw is None or anim.bones[jaw].parent_id < 0 or not ch[jaw]:
        return False
    jaw_set = set(_subtree(ch, jaw))
    head_set = set(_subtree(ch, anim.bones[jaw].parent_id)) - jaw_set
    head = pts[np.isin(bone_of, list(head_set))]
    if len(head) < 6:
        return False
    hz = g[jaw][2, 3]
    hs = head[head[:, 2] >= hz]     # the head in front of the hinge (the neck junction overlaps the body)
    front = head[head[:, 2] >= head[:, 2].max() - 1.5 * pitch]
    if len(hs) < 2 or front[:, 2].mean() <= hz + 2 * pitch:
        return False
    hinge = np.array([g[jaw][0, 3], hs[:, 1].min() + JAW_HINGE_FRAC * (hs[:, 1].max() - hs[:, 1].min()), hz])
    tip = np.array([g[jaw][0, 3], (front[:, 1].min() + front[:, 1].max()) / 2, front[:, 2].mean()])
    d = hinge - g[jaw][:3, 3]
    for i in jaw_set:
        g[i][:3, 3] += d
    tip_id = ch[jaw][0]
    old_tip = g[tip_id][:3, 3].copy()
    for i in _subtree(ch, tip_id):
        g[i][:3, 3] += tip - old_tip
    return True


def _mouth_split(anim, g, ch, pts, segs, dist, bone_of, d_best, allow, seg_allow) -> int:
    """A jaw has no envelope of its own (forge rigs bind 0 boxes to a completed jaw), so plain
    assignment hands the whole head to the skull and the mouth can never open. Head voxels (bound
    to the jaw's parent subtree) in front of the hinge and below the line Jaw -> JawTip go to the
    nearest jaw segment. In-place on bone_of / d_best / allow; returns the jaw voxel count."""
    jaw = next((b.id for b in anim.bones if b.name.lower() == "jaw"), None)
    if jaw is None or anim.bones[jaw].parent_id < 0:
        return 0
    jaw_set = set(_subtree(ch, jaw))
    head_set = set(_subtree(ch, anim.bones[jaw].parent_id)) - jaw_set
    hinge = g[jaw][:3, 3]
    tip_id = next((c for c in ch[jaw]), jaw)
    tip = g[tip_id][:3, 3]
    dz = tip[2] - hinge[2]
    slope = (tip[1] - hinge[1]) / dz if abs(dz) > 1e-6 else 0.0
    line_y = hinge[1] + slope * (pts[:, 2] - hinge[2])
    sel = np.isin(bone_of, list(head_set)) & (pts[:, 2] > hinge[2]) & (pts[:, 1] < line_y)
    cols = [k for k, (bid, _, _) in enumerate(segs) if bid in jaw_set]
    if not sel.any() or not cols:
        return 0
    idx = np.where(sel)[0]
    sub_d = dist[np.ix_(idx, cols)]
    pick = np.array(cols)[sub_d.argmin(axis=1)]
    bone_of[idx] = [segs[k][0] for k in pick]
    d_best[idx] = dist[idx, pick]
    # the jaw is a lower part of the head: its allowance is the head bone's it was taken from
    allow[idx] = np.maximum(allow[idx], seg_allow[pick])
    return int(len(idx))


def bind(vox, rig_path, body_kind: str | None = None, palette_rgb=None, palette_index=None):
    """Return (AnimFile, metrics). vox is a voxelize.Voxels."""
    rig = rig_path if isinstance(rig_path, af.AnimFile) else af.parse(Path(rig_path))
    out = copy.deepcopy(rig)
    body_kind = body_kind or vox.body_kind
    pts = np.asarray(vox.centers, dtype=float)
    pitch = float(vox.pitch)
    g = fk(out)
    ch = _children(out)

    # --- 2. global fit from the rig's own body cloud to the voxel cloud ---------------------
    if rig.boxes:
        rig_cloud = np.array([(g[x.bone_id] @ np.r_[x.center, 1.0])[:3] for x in rig.boxes])
    else:
        rig_cloud = np.array([g[b.id][:3, 3] for b in rig.bones])
    rlo, rhi = rig_cloud.min(0), rig_cloud.max(0)
    vlo, vhi = pts.min(0) - pitch / 2, pts.max(0) + pitch / 2
    if body_kind == "biped":
        s = np.full(3, (vhi[1] - vlo[1]) / (rhi[1] - rlo[1]))
    else:
        s = (vhi - vlo) / np.maximum(rhi - rlo, 1e-6)
    t = (vlo + vhi) / 2 - s * (rlo + rhi) / 2
    t[1] = vlo[1] - s[1] * rlo[1]
    if body_kind == "biped":
        # centre on the hips/lower torso, not the bbox: arms bent forward pulled the bbox centre
        # and left the hobgoblin's spine 0.19 u in front of its body (2026-10-02)
        hm, hr = vhi[1] - vlo[1], rhi[1] - rlo[1]
        mb = pts[(pts[:, 1] > vlo[1] + 0.45 * hm) & (pts[:, 1] < vlo[1] + 0.6 * hm)]
        rb = rig_cloud[(rig_cloud[:, 1] > rlo[1] + 0.45 * hr) & (rig_cloud[:, 1] < rlo[1] + 0.6 * hr)]
        if len(mb) and len(rb):
            t[0] = np.median(mb[:, 0]) - s[0] * np.median(rb[:, 0])
            t[2] = np.median(mb[:, 2]) - s[2] * np.median(rb[:, 2])
    for i in g:
        g[i][:3, 3] = s * g[i][:3, 3] + t
    height = float(vhi[1] - vlo[1])
    g_src = fk(rig)
    if body_kind != "biped":
        # per-axis scaling distorts a bent leg (oracle 2026-10-02: owlbear knee inversion 0.27 vs
        # the bear's own 0.17 at s = 2.7 x 1.6 x 1.5): legs are scaled UNIFORMLY by the height
        # scale about their mapped root, so their joint angles mean what the clips assume
        for root, _ in _leg_roots(out, g, ch, height):
            r_old, r_new = g_src[root][:3, 3], g[root][:3, 3].copy()
            for i in _subtree(ch, root):
                g[i][:3, 3] = r_new + s[1] * (g_src[i][:3, 3] - r_old)

    spine_fitted = _fit_biped_spine(out, g, ch, pts, height, pitch) if body_kind == "biped" else 0
    arms = _fit_arms(out, g, ch, pts, height, pitch) if body_kind == "biped" else {}

    # --- 3. legs onto the mesh's legs -----------------------------------------------------------
    # Each rig leg is matched to one GROUND CONTACT (a connected cluster of near-ground voxels) by
    # its own foot position, never across sides. The old front/back half split put two mesh legs
    # of a mid-stride panther under one rig leg and left the other with 0 voxels (2026-10-02).
    # Fewer contacts than legs (feet touching) -> the half split is the fallback.
    gside = _geo_sides(out, g, height)
    legs = _leg_roots(out, g, ch, height)
    fitted_cloud = s * rig_cloud + t
    rig_bone = np.array([x.bone_id for x in rig.boxes]) if rig.boxes else np.array([b.id for b in rig.bones])
    moved = []
    contacts_used = 0
    if legs:
        refs, sides = [], []
        for root, foot in legs:
            # reference = the rig's OWN low geometry for this leg (its foot boxes), not the leaf
            # joint: the humanoid's leaf is the toe tip (orc, 2026-10-02)
            ref = fitted_cloud[np.isin(rig_bone, _subtree(ch, root)) & (fitted_cloud[:, 1] < 0.25 * height)]
            refs.append((ref[:, 0].mean(), ref[:, 2].mean()) if len(ref) else (g[foot][0, 3], g[foot][2, 3]))
            sides.append(gside.get(root) or gside.get(foot) or int(np.sign(refs[-1][0])))
        contacts = [c for c in _ground_contacts(pts[pts[:, 1] < 0.15 * height], pitch) if len(c) >= 3]
        targets = [None] * len(legs)
        if len(contacts) >= len(legs):
            from scipy.optimize import linear_sum_assignment
            cen = np.array([c.mean(axis=0) for c in contacts])
            cost = np.zeros((len(legs), len(contacts)))
            for i, ((rx, rz), sd) in enumerate(zip(refs, sides)):
                cost[i] = np.hypot(cen[:, 0] - rx, cen[:, 2] - rz)
                csd = np.where(cen[:, 0] > pitch, 1, np.where(cen[:, 0] < -pitch, -1, 0))
                cost[i, (sd != 0) & (csd != 0) & (csd != sd)] = 1e6
            rows, cols = linear_sum_assignment(cost)
            for r, c in zip(rows, cols):
                if cost[r, c] < 1e5:
                    targets[r] = (cen[c, 0], cen[c, 2])
                    contacts_used += 1
        if contacts_used < len(legs):
            foot_z = np.array([g[f][2, 3] for _, f in legs])
            z_mid = float(np.median(foot_z))
            multi_row = foot_z.max() - foot_z.min() > 4 * pitch
            low = pts[pts[:, 1] < 0.25 * height]
            for i, (root, foot) in enumerate(legs):
                if targets[i] is not None:
                    continue
                sd = sides[i]
                sel = low[sd * low[:, 0] > pitch / 2] if sd else low
                if multi_row:
                    front = g[foot][2, 3] >= z_mid
                    sel = sel[(sel[:, 2] >= z_mid) == front]
                if len(sel):
                    targets[i] = (sel[:, 0].mean(), sel[:, 2].mean())
        for i, (root, foot) in enumerate(legs):
            if targets[i] is None:
                continue
            dx = float(targets[i][0] - refs[i][0])
            dz = float(targets[i][1] - refs[i][1])
            for j in _subtree(ch, root):
                g[j][0, 3] += dx
                g[j][2, 3] += dz
            moved.append(out.bones[root].name)

    axis_moved = _fit_axis(out, g, ch, pts, pitch, legs) if body_kind != "biped" else 0

    # NOTE (2026-10-02): a per-joint creature girth (mesh vs rig cross-section, clamped 1..2) was
    # tried for the bulky Meshy spiders and REVERTED: it let a quadruped bound to the serpent
    # skeleton pass at 0.998 — the bar could no longer tell a wrong skeleton from a fat one.
    local_girth = {}

    _rebuild_locals(out, g)
    g = fk(out)

    # --- 4. assignment ------------------------------------------------------------------------
    segs = _segments(out, g, ch)
    dist = np.stack([_seg_dist(pts, a, b) for _, a, b in segs], axis=1)
    seg_side = np.array([gside.get(bid, 0) for bid, _, _ in segs])
    vside = np.where(pts[:, 0] > pitch, 1, np.where(pts[:, 0] < -pitch, -1, 0))
    forbidden = (seg_side[None, :] != 0) & (vside[:, None] != 0) & (seg_side[None, :] != vside[:, None])
    # distance relative to each bone's own thickness: torso skin prefers the thick spine bones
    # over a thin head bone that happens to be a little nearer (measured 2026-10-01: plain
    # nearest-segment bound 16 front-torso voxels of the quadruped fixture to HeadRoot)
    extent = float(vox.extent_u)
    env = bone_envelopes(rig)
    s_mean = float(np.cbrt(np.prod(s)))
    girth = 1.0
    if body_kind == "biped":
        # bipeds are fitted by height only, so a bulky body (the orc's armour, 2026-10-02) never
        # reaches the allowance: scale it by torso depth, mesh vs rig, clamped to 1..2
        def depth(cloud, h):
            band = cloud[(cloud[:, 1] > 0.55 * h) & (cloud[:, 1] < 0.75 * h)]
            band = band[np.abs(band[:, 0] - np.median(band[:, 0])) < 0.1 * h]
            return float(np.ptp(band[:, 2])) if len(band) > 3 else 0.0
        dm, dr = depth(pts, height), depth(s * rig_cloud + t, height)
        if dm > 0 and dr > 0:
            girth = float(np.clip(dm / dr, 1.0, 2.0))
    s_mean *= girth
    seg_allow = np.array([max(0.15 * extent, 1.25 * env.get(int(bid), 0.0) * s_mean * local_girth.get(int(bid), 1.0))
                          for bid, _, _ in segs])
    best = np.where(forbidden, np.inf, dist / seg_allow[None, :]).argmin(axis=1)
    bone_of = np.array([segs[k][0] for k in best])
    if body_kind != "biped" and _fit_jaw(out, g, ch, pts, bone_of, pitch):
        # the jaw joints moved onto the mesh: rebuild the bind pose and assign again
        _rebuild_locals(out, g)
        g = fk(out)
        segs = _segments(out, g, ch)
        dist = np.stack([_seg_dist(pts, a, b) for _, a, b in segs], axis=1)
        seg_side = np.array([gside.get(bid, 0) for bid, _, _ in segs])
        forbidden = (seg_side[None, :] != 0) & (vside[:, None] != 0) & (seg_side[None, :] != vside[:, None])
        seg_allow = np.array([max(0.15 * extent, 1.25 * env.get(int(bid), 0.0) * s_mean * local_girth.get(int(bid), 1.0))
                          for bid, _, _ in segs])
        best = np.where(forbidden, np.inf, dist / seg_allow[None, :]).argmin(axis=1)
        bone_of = np.array([segs[k][0] for k in best])
    d_best = dist[np.arange(len(pts)), best]
    allow = seg_allow[best]
    jaw_n = _mouth_split(out, g, ch, pts, segs, dist, bone_of, d_best, allow, seg_allow)

    # --- metrics ------------------------------------------------------------------------------
    within = float((d_best <= allow).mean())
    within_flat = float((d_best <= 0.15 * extent).mean())
    bone_side = np.array([gside.get(int(b), 0) for b in bone_of])
    side_viol = int(((vside != 0) & (bone_side == -vside)).sum())
    feet_err = 0.0
    for root, _ in legs:
        m = np.isin(bone_of, _subtree(ch, root))
        feet_err = max(feet_err, float(pts[m, 1].min() - pitch / 2) if m.any() else extent)

    # --- 5. emit ------------------------------------------------------------------------------
    inv = {b.id: np.linalg.inv(g[b.id]) for b in out.bones}
    if palette_rgb is None:
        cols = np.asarray(vox.colors, dtype=float)
    else:
        cols = np.asarray(palette_rgb, dtype=float)[np.asarray(palette_index)]
    out.boxes = []
    for i, p in enumerate(pts):
        bid = int(bone_of[i])
        c = (inv[bid] @ np.r_[p, 1.0])[:3]
        out.boxes.append(af.BoxShape(bone_id=bid, size=(pitch, pitch, pitch),
                                     center=tuple(float(round(x, 6)) for x in c),
                                     color=tuple(float(round(x / 255.0, 6)) for x in cols[i])))
    # a key's offset from the bind pose scales with the fit, and is applied to the FITTED bind:
    # mapping keys as s*key+t ignored the axis/leg fits and lifted the owlbear 0.46 u (2026-10-01)
    # a clip's Speed line is the body speed its feet imply; at the same joint angles the stride
    # scales with leg length (oracle 2026-10-02: owlbear walk speed mismatch 1.08 vs bear 0.46)
    ratios = []
    for root, foot in legs:
        chain = _path_to_root(out, foot)
        chain = chain[chain.index(root):]
        old_len = sum(np.linalg.norm(g_src[b][:3, 3] - g_src[a][:3, 3]) for a, b in zip(chain, chain[1:]))
        new_len = sum(np.linalg.norm(g[b][:3, 3] - g[a][:3, 3]) for a, b in zip(chain, chain[1:]))
        if old_len > 1e-6:
            ratios.append(new_len / old_len)
    speed_scale = float(np.mean(ratios)) if ratios else float(s[1])
    old_bind = {b.id: np.asarray(b.pos, dtype=float) for b in rig.bones}
    new_bind = {b.id: np.asarray(b.pos, dtype=float) for b in out.bones}
    for clip in out.clips:
        for chn in clip.channels:
            if not chn.pos_keys:
                continue
            ob, nb = old_bind[chn.bone_id], new_bind[chn.bone_id]
            chn.pos_keys = [(k, tuple(float(round(v, 6)) for v in nb + s * (np.asarray(p) - ob)))
                            for k, p in chn.pos_keys]

    jaw_ids = [b.id for b in out.bones if b.name.lower() == "jaw"]
    jaw_all = set(_subtree(ch, jaw_ids[0])) if jaw_ids else set()
    # clip Speed lines: locomotion clips get the MEASURED stance-speed ratio (import vs source,
    # same clip, same feet); everything else the leg-length ratio. Leg length alone left the
    # black bear's walk 0.72 off vs the bear's own 0.46 (oracle, 2026-10-02).
    import anim_lint
    feet = [f for _, f in legs]
    src_h = float(rig_cloud[:, 1].max() - rig_cloud[:, 1].min())
    speed_measured = {}
    for clip, src_clip in zip(out.clips, rig.clips):
        if not clip.speed:
            continue
        ratio = speed_scale
        if feet and anim_lint.LOCOMOTION_NAME_RE.search(clip.name) and not anim_lint.TRANSITION_NAME_RE.search(clip.name):
            # where anim_lint can find the feet (ToeBase/Foot names) ITS estimate is the reference,
            # since its lint gate judges the Speed line; elsewhere the binder's own estimate
            la, lb = anim_lint.foot_slide_metrics(rig, src_clip), anim_lint.foot_slide_metrics(out, clip)
            if "error" not in la and "error" not in lb:
                a, b = la["est_speed"], lb["est_speed"]
            else:
                a = _stance_speed(rig, src_clip, feet, src_h)
                b = _stance_speed(out, clip, feet, height)
            if a > 1e-3 and b > 1e-3:
                ratio = b / a
                speed_measured[clip.name] = round(ratio, 4)
        clip.speed = float(round(clip.speed * ratio, 6))

    metrics = {"jaw_voxels": int(np.isin(bone_of, list(jaw_all)).sum()) if jaw_all else 0,
               "jaw_split_voxels": int(jaw_n), "within_frac": round(within, 4), "within_frac_flat": round(within_flat, 4), "side_violations": side_viol,
               "feet_ground_err_u": round(feet_err, 4), "legs_fitted": moved, "ground_contacts_used": contacts_used, "spine_joints_fitted": spine_fitted, "arms_fitted": arms, "axis_joints_fitted": axis_moved,
               "scale": [round(float(x), 4) for x in s], "girth": round(girth, 3), "speed_scale": round(speed_scale, 4), "speed_measured": speed_measured, "bones_used": int(len(np.unique(bone_of)))}
    return out, metrics
