"""Pipeline orchestration: spec -> Compiled(.anim AnimFile + voxel model).

Stages (ForgePattern): GROUNDED PARAMS (spec validation) -> PLAN (joints,
skeleton) -> RASTERIZE (sweep + parts -> voxel grid -> greedy boxes) ->
EMIT (AnimFile, clips, ground_ref, walk Speed, provenance header).

Engine contracts honoured here:
* bone ids are line order, parents precede children (skeleton build order)
* every box carries an explicit sRGB color (no appearance-palette fallback)
* clips: 'move' aliases to 'walk'; walk Speed measured via
  finalize_quadruped.measure_walk_speed; ground_ref appended last
* target_height rescales the FINISHED rig uniformly (bones, boxes, pos keys)
  so spec-space math stays untouched
"""
from __future__ import annotations

import hashlib
import json
from dataclasses import dataclass, field
from typing import Optional

from anim_format import AnimFile, Bone, BoxShape
from finalize_quadruped import ensure_ground_ref, measure_walk_speed

from . import __version__
from .animate import compile_clips
from .joints import resolve
from .parts import build_parts
from .skeleton import build as build_skeleton
from .skeleton import detect_morphology
from .spec import SpecError, hex_to_rgb, validate_basic
from .sweep import volume_samples
from .voxelize import Grid, apply_shading, greedy_merge, stamp_volume


@dataclass
class Options:
    voxel_size: float = 0.05      # fauna precedent (extract_animation.py default)
    target_height: Optional[float] = None
    samples: int = 24             # animation resample rate per cycle
    noise: bool = True
    body_plan: bool = True


@dataclass
class Compiled:
    af: AnimFile
    spec: dict
    sk: object
    grid: Grid
    options: Options
    warnings: list = field(default_factory=list)
    direct_box_bones: set = field(default_factory=set)

    def bind_world_positions(self) -> dict:
        """Bone name -> bind world position, from the finished AnimFile
        (identity bind rotations => world = accumulated local offsets)."""
        out = {}
        pos_by_id = {}
        for b in self.af.bones:
            if b.parent_id < 0:
                p = b.pos
            else:
                pp = pos_by_id[b.parent_id]
                p = (pp[0] + b.pos[0], pp[1] + b.pos[1], pp[2] + b.pos[2])
            pos_by_id[b.id] = p
            out[b.name] = p
        return out


def _spec_hash(spec: dict) -> str:
    return hashlib.sha1(
        json.dumps(spec, sort_keys=True).encode("utf-8")).hexdigest()[:12]


def compile_spec(spec: dict, options: Options = None) -> Compiled:
    options = options or Options()
    validate_basic(spec)

    joints, joints_r = resolve(spec)
    sk = build_skeleton(spec, joints, joints_r)
    warnings = []

    morph = detect_morphology(sk.names)
    if morph == "unknown":
        warnings.append(
            "bone names satisfy no engine morphology heuristic "
            "(detectMorphology -> Unknown); quadrupeds want exact 'Pelvis' + "
            "'Chest' plus a *Tail*/*Paw* bone")

    # ---- RASTERIZE: volumes ------------------------------------------------
    vs = options.voxel_size
    step = vs * 0.5
    grid = Grid(vs)
    sections = spec.get("sections", {})
    palette = spec["palette"]
    volume_eval = {}

    for vol in spec.get("volumes", []):
        if vol.get("faceted") and spec.get("build") != "rigid":
            raise SpecError("faceted volumes require build:'rigid'")
        base_rgb = hex_to_rgb(palette[vol["material"]]["color"])
        arcs = (vol.get("colors") or {}).get("arcs")
        chain = vol["chain"]
        samples, _total = volume_samples(vol, sk.chains[chain], sk.world, step)
        stamp_volume(grid, samples, base_rgb, arcs, step, sections)
        volume_eval.setdefault(chain, samples)
        rchain = sk.mirror_chain.get(chain)
        if rchain:
            # re-sweep the mirrored chain from its own (possibly joints_R
            # adjusted) world positions; named sections would mirror with the
            # wrong handedness — warned, not silently accepted
            if any(s.opts.get("section") for s in samples):
                warnings.append(
                    f"volume on mirrored chain '{chain}' uses a named section; "
                    "the R side re-sweeps un-mirrored (handedness)")
            rsamples, _ = volume_samples(vol, sk.chains[rchain], sk.world, step)
            stamp_volume(grid, rsamples, base_rgb, arcs, step, sections)

    # ---- RASTERIZE: parts --------------------------------------------------
    direct_boxes = []  # (bone_name, size, center_world, rgb)
    build_parts(spec, sk, joints, grid, volume_eval, step, direct_boxes, warnings)

    # ---- colors ------------------------------------------------------------
    shading = dict(spec.get("shading") or {})
    if not options.noise:
        shading["noise"] = {"amount": 0.0}
    apply_shading(grid, shading)

    # ---- merge -> boxes ----------------------------------------------------
    merged = greedy_merge(grid)

    # ---- EMIT: AnimFile ----------------------------------------------------
    af = AnimFile()
    af.header_comments.append(
        f"# generated-by: creature_forge v{__version__} "
        f"spec={spec.get('name', 'unnamed')} sha1={_spec_hash(spec)} "
        f"voxel={vs}")
    for name in sk.names:
        idx = sk.index[name]
        parent = sk.parents[idx]
        world = sk.world[name]
        if parent < 0:
            local = world
        else:
            pw = sk.world[sk.names[parent]]
            local = (world[0] - pw[0], world[1] - pw[1], world[2] - pw[2])
        af.bones.append(Bone(id=idx, name=name, parent_id=parent,
                             pos=local, rot=(0.0, 0.0, 0.0, 1.0),
                             scale=(1.0, 1.0, 1.0)))

    boxes = []
    for bone_name, size, center_w, rgb in merged:
        bw = sk.world[bone_name]
        boxes.append(BoxShape(
            bone_id=sk.index[bone_name], size=size,
            center=(center_w[0] - bw[0], center_w[1] - bw[1], center_w[2] - bw[2]),
            color=rgb))
    direct_bones = set()
    for bone_name, size, center_w, rgb in direct_boxes:
        bw = sk.world[bone_name]
        direct_bones.add(bone_name)
        boxes.append(BoxShape(
            bone_id=sk.index[bone_name], size=size,
            center=(center_w[0] - bw[0], center_w[1] - bw[1], center_w[2] - bw[2]),
            color=rgb))
    boxes.sort(key=lambda b: (b.bone_id, b.center, b.size))
    af.boxes = boxes

    # engine quirk: per-box colors are only honoured when a bone owns >1 box
    per_bone = {}
    for b in boxes:
        per_bone[b.bone_id] = per_bone.get(b.bone_id, 0) + 1
    for bid, count in sorted(per_bone.items()):
        if count == 1:
            warnings.append(
                f"bone '{sk.names[bid]}' owns exactly 1 box; the engine "
                "ignores single-box explicit colors (appearance fallback)")

    # ---- clips -------------------------------------------------------------
    bind_local = {b.name: b.pos for b in af.bones}
    af.clips = compile_clips(spec, sk, bind_local, samples=options.samples)

    # combat metadata: the engine reads '# clip_meta:' header lines for the
    # damage timing (hitFrameFraction) — without it the default 0.4 applies
    if af.clip("attack") is not None:
        atk_spec = spec.get("animations", {}).get("attack", {})
        af.set_clip_meta("attack", {
            "type": "combat",
            "hitFrameFraction": float(atk_spec.get("hit_fraction", 0.45)),
            "meleeFamily": "natural",
            "meleeRole": "primary",
        })

    # ---- target height rescale --------------------------------------------
    if options.target_height:
        ys = []
        world_pos = {b.id: None for b in af.bones}
        acc = {}
        for b in af.bones:
            p = b.pos if b.parent_id < 0 else tuple(
                acc[b.parent_id][k] + b.pos[k] for k in range(3))
            acc[b.id] = p
        for bx in af.boxes:
            bp = acc[bx.bone_id]
            ys.append(bp[1] + bx.center[1] - bx.size[1] / 2)
            ys.append(bp[1] + bx.center[1] + bx.size[1] / 2)
        height = (max(ys) - min(ys)) if ys else 1.0
        s = options.target_height / height
        for b in af.bones:
            b.pos = tuple(c * s for c in b.pos)
        for bx in af.boxes:
            bx.size = tuple(c * s for c in bx.size)
            bx.center = tuple(c * s for c in bx.center)
        for clip in af.clips:
            for ch in clip.channels:
                ch.pos_keys = [(t, tuple(c * s for c in v))
                               for t, v in ch.pos_keys]

    # ---- finalize: grounding + walk speed ---------------------------------
    ensure_ground_ref(af)
    walk = af.clip("walk")
    if walk is not None:
        # measure_walk_speed derives the no-slide speed from how far a planted
        # FOOT sweeps during stance. A legless creature has no stance, so the
        # measurement degenerates to ~0 and the engine would translate a frozen
        # snake across the ground — hence the spec-authored override.
        override = spec.get("walk_speed")
        spd = measure_walk_speed(af, clip_name="walk")
        if override:
            walk.speed = round(float(override), 3)
            if spd and abs(spd - override) > 0.5 * override:
                warnings.append(
                    f"spec walk_speed {override} overrides the measured "
                    f"{spd:.3f} — intended for legless/gliding rigs; a legged "
                    "creature should trust the measurement")
        elif spd and spd >= 0.05:
            walk.speed = round(spd, 3)
        else:
            warnings.append(
                f"walk speed measurement degenerate ({spd if spd else 0:.3f}) — "
                "no planted foot to measure. Set spec 'walk_speed' explicitly "
                "or the creature will slide")

    return Compiled(af=af, spec=spec, sk=sk, grid=grid, options=options,
                    warnings=warnings, direct_box_bones=direct_bones)
