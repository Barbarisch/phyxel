"""Bone-box interpenetration metric — where does an appearance preset break?

Replicates the engine's proportion pipeline in Python (getLimbScales /
applySkeletonProportions / buildBodiesFromModel aggregate path, humanoid
branch), poses the skeleton through a clip, and measures pairwise overlap
volume between different bones' box AABBs. Direct parent-child pairs are
excluded (joints naturally overlap); everything else counts as clipping.

The number that matters: overlap_pct = total foreign-pair overlap volume /
total box volume, worst posed frame. Compare presets against the "standard"
baseline — the DELTA over standard is the preset-induced clipping.

Used by `anim_lint.py clipcheck` and tests/test_preset_clipping.py.
Limitations: boxes are treated as world-axis AABBs after posing (conservative)
and postureLeanDeg is not simulated.
"""
from __future__ import annotations

import json
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from anim_format import parse, AnimFile  # noqa: E402
from anim_lint import sample_rotation, sample_position  # noqa: E402

REPO = Path(__file__).resolve().parents[2]
PRESETS_JSON = REPO / "resources" / "appearance_presets.json"

SKIP_BONES = ("thumb", "index", "middle", "ring", "pinky", "eye", "toe", "end")

DEFAULT_APPEARANCE = {
    "heightScale": 1.0, "bulkScale": 1.0, "headScale": 1.0,
    "armLengthScale": 1.0, "legLengthScale": 1.0, "torsoLengthScale": 1.0,
    "shoulderWidthScale": 1.0, "bellyScale": 1.0,
}


def load_preset(preset_id: str) -> dict:
    doc = json.loads(PRESETS_JSON.read_text(encoding="utf-8"))
    for entry in doc["presets"]:
        if entry["presetId"] == preset_id:
            app = dict(DEFAULT_APPEARANCE)
            for k in app:
                app[k] = entry.get(k, app[k])
            return app
    raise KeyError(f"unknown preset {preset_id!r}")


def limb_scales(name_lower: str, app: dict):
    """Mirror of AnimatedVoxelCharacter getLimbScales, Humanoid branch —
    keep the check ORDER identical to the C++."""
    length, thick = app["heightScale"], app["bulkScale"]
    if "head" in name_lower or "neck" in name_lower:
        if "head" in name_lower:
            length = thick = app["headScale"]
        else:
            length = app["heightScale"] * app["torsoLengthScale"]
    elif "arm" in name_lower or "forearm" in name_lower or "hand" in name_lower:
        length = app["heightScale"] * app["armLengthScale"]
    elif "shoulder" in name_lower:
        length = app["shoulderWidthScale"]
        thick = app["bulkScale"] * app["shoulderWidthScale"]
    elif "leg" in name_lower or "upleg" in name_lower or "foot" in name_lower:
        length = app["heightScale"] * app["legLengthScale"]
    elif "spine" in name_lower or "chest" in name_lower:
        length = app["heightScale"] * app["torsoLengthScale"]
    elif "hip" in name_lower:
        length = app["heightScale"]
        thick = app["bulkScale"]
    return length, thick


def qmat(q):
    x, y, z, w = q
    return (
        (1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)),
        (2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)),
        (2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)),
    )


def mat_mul(a, b):
    return tuple(tuple(sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3))
                 for i in range(3))


def mat_vec(m, v):
    return tuple(sum(m[i][k] * v[k] for k in range(3)) for i in range(3))


def compute_bone_data(af: AnimFile, app: dict):
    """Scaled bind positions + per-bone aggregate box (engine aggregate path)."""
    scaled_pos = {}
    for b in af.bones:
        low = b.name.lower()
        if b.parent_id == -1:
            scaled_pos[b.id] = tuple(b.pos)
        else:
            length, _ = limb_scales(low, app)
            scaled_pos[b.id] = tuple(c * length for c in b.pos)

    by_bone = {}
    for s in af.boxes:
        by_bone.setdefault(s.bone_id, []).append(s)

    boxes = {}
    for bone_id, shapes in by_bone.items():
        name_low = af.bones[bone_id].name.lower()
        if any(k in name_low for k in SKIP_BONES):
            continue
        mn = [min(s.center[i] - s.size[i] / 2 for s in shapes) for i in range(3)]
        mx = [max(s.center[i] + s.size[i] / 2 for s in shapes) for i in range(3)]
        size = [max(mx[i] - mn[i], 0.05) for i in range(3)]
        center = [(mn[i] + mx[i]) / 2 for i in range(3)]
        length, thick = limb_scales(name_low, app)
        belly = ("spine" in name_low and "spine2" not in name_low) or "hip" in name_low
        bz = app["bellyScale"] if belly else 1.0
        bx = 1.0 + (app["bellyScale"] - 1.0) * 0.5 if belly else 1.0
        if "head" in name_low:
            size = [c * app["headScale"] for c in size]
            center = [c * app["headScale"] for c in center]
        else:
            size = [size[0] * thick * bx, size[1] * length, size[2] * thick * bz]
            center = [center[0], center[1] * length, center[2]]
        boxes[bone_id] = (size, center)
    return scaled_pos, boxes


def pose_globals(af: AnimFile, app: dict, scaled_pos, clip=None, t=0.0):
    """Global (rot, pos) per bone at time t (bind pose if clip is None).
    Animation position keys are scaled by the same limb length, mirroring
    applySkeletonProportions."""
    channels = {}
    if clip is not None:
        channels = {ch.bone_id: ch for ch in clip.channels}

    glob = {}
    for b in af.bones:
        low = b.name.lower()
        local_pos = scaled_pos[b.id]
        local_rot = tuple(b.rot)
        ch = channels.get(b.id)
        if ch is not None:
            if ch.rot_keys:
                local_rot = sample_rotation(ch.rot_keys, t)
            if ch.pos_keys and b.parent_id != -1:
                length, _ = limb_scales(low, app)
                p = sample_position(ch.pos_keys, t)
                local_pos = tuple(c * length for c in p)
        rot_m = qmat(local_rot)
        if b.parent_id == -1:
            glob[b.id] = (rot_m, local_pos)
        else:
            prot, ppos = glob[b.parent_id]
            world = mat_vec(prot, local_pos)
            glob[b.id] = (mat_mul(prot, rot_m),
                          tuple(ppos[i] + world[i] for i in range(3)))
    return glob


def world_aabb(rot, pos, size, center):
    """Conservative world AABB of a rotated box."""
    c_world = mat_vec(rot, center)
    c = tuple(pos[i] + c_world[i] for i in range(3))
    half = [0.0, 0.0, 0.0]
    for i in range(3):
        half[i] = sum(abs(rot[i][j]) * size[j] / 2 for j in range(3))
    return tuple(c[i] - half[i] for i in range(3)), tuple(c[i] + half[i] for i in range(3))


def overlap_volume(a, b):
    v = 1.0
    for i in range(3):
        lo = max(a[0][i], b[0][i])
        hi = min(a[1][i], b[1][i])
        if hi <= lo:
            return 0.0
        v *= hi - lo
    return v


def clip_metric(anim_path, app: dict, clip_name="walk", samples=8):
    """Worst-frame foreign-pair overlap. Returns dict with overlap_pct and
    the worst offending bone pairs."""
    af = parse(str(anim_path))
    scaled_pos, boxes = compute_bone_data(af, app)
    total_volume = sum(s[0] * s[1] * s[2] for s, _ in boxes.values()) or 1.0

    parent = {b.id: b.parent_id for b in af.bones}

    def adjacent(i, j):
        return parent.get(i) == j or parent.get(j) == i

    clip = af.clip(clip_name)
    times = [None]  # bind pose
    if clip is not None and clip.duration > 0:
        times += [clip.duration * k / samples for k in range(samples)]

    worst = {"overlap_pct": 0.0, "frame": "bind", "pairs": []}
    ids = sorted(boxes)
    for t in times:
        glob = pose_globals(af, app, scaled_pos,
                            None if t is None else clip, 0.0 if t is None else t)
        aabbs = {i: world_aabb(*glob[i], *boxes[i]) for i in ids}
        total = 0.0
        pairs = []
        for a_i in range(len(ids)):
            for b_i in range(a_i + 1, len(ids)):
                i, j = ids[a_i], ids[b_i]
                if adjacent(i, j):
                    continue
                v = overlap_volume(aabbs[i], aabbs[j])
                if v > 0:
                    total += v
                    pairs.append((v, af.bones[i].name, af.bones[j].name))
        pct = 100.0 * total / total_volume
        if pct > worst["overlap_pct"]:
            pairs.sort(reverse=True)
            worst = {"overlap_pct": pct,
                     "frame": "bind" if t is None else f"{clip_name}@{t:.2f}s",
                     "pairs": [(round(v, 5), a.replace('mixamorig:', ''),
                                b.replace('mixamorig:', '')) for v, a, b in pairs[:5]]}
    return worst
