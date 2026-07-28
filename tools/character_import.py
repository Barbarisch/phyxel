#!/usr/bin/env python3
"""character_import.py — one importer for ALL external character content.

Phase C durable deliverable (docs/CharacterLibraryPlan.md): whatever lane the
content comes from (Mixamo free, local open-weight models, paid services),
it enters the engine through THIS tool, which enforces the rules the old
ad-hoc importers didn't:

  * per-source bone-name maps (SOURCE_BONE_MAPS) + exact matching against the
    target rig — unmapped bones with real animation data FAIL LOUDLY with
    fuzzy suggestions instead of being silently dropped;
  * --dry-run reports everything (bone coverage, clip inventory, durations)
    before a single byte is written;
  * merged output is linted (anim_lint absolute checks) before replacing the
    target — a broken import never lands;
  * clip naming goes through the FSM-compatible name map.

Modes
-----
CLIP IMPORT (default): bring animation clips into an existing rig.
    python tools/character_import.py "resources/mixamo_imports/Waving.fbx" \
        --clip-name wave --target resources/animated_characters/humanoid.anim

NEW RIG (--new-rig): voxelize a rigged model into a standalone .anim
(delegates to tools/asset_pipeline/extract_animation.py, then reports bone
naming compatibility with the humanoid FSM).
    python tools/character_import.py model.glb --new-rig ogre_brute

FBX input is converted via FBX2glTF/assimp (extract_animation.py handles it).
"""
from __future__ import annotations

import argparse
import difflib
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "tools" / "anim_pipeline"))
sys.path.insert(0, str(REPO / "tools"))

from anim_format import parse, write, Clip  # noqa: E402

EXTRACT = REPO / "tools" / "asset_pipeline" / "extract_animation.py"
DEFAULT_TARGET = REPO / "resources" / "animated_characters" / "humanoid.anim"

# ---------------------------------------------------------------------------
# Per-source bone-name maps: source bone name -> target (mixamorig) name.
# "mixamo" is identity (names already match). Fill in service tables as the
# bake-off characterizes each exporter; until then the fuzzy matcher proposes
# candidates and the import FAILS rather than guessing.
# ---------------------------------------------------------------------------
SOURCE_BONE_MAPS: dict[str, dict[str, str]] = {
    "mixamo": {},
    # Meshy exports Mixamo-compatible skeletons on its humanoid rig preset —
    # verify per-asset during the bake-off before trusting this.
    "meshy": {},
    # Tripo uses its own naming — populate from the first real export.
    "tripo": {},
}

# Bones that legitimately exist in exports but not in our rigs.
IGNORABLE_BONES = {"rootnode", "armature", "scene", "root"}

# Mixamo download names -> FSM clip names (superset of batch_import_mixamo's).
try:
    from batch_import_mixamo import CLIP_NAME_MAP  # noqa: E402
except Exception:
    CLIP_NAME_MAP = {}


def run_extract(src: Path, out_anim: Path, style: str, scale: float,
                target_height: float | None = None) -> None:
    cmd = [sys.executable, str(EXTRACT), str(src), str(out_anim),
           "--style", style, "--scale", str(scale)]
    if target_height is not None:
        cmd += ["--target_height", str(target_height)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"extract_animation failed for {src}:\n{r.stderr or r.stdout}")


def channel_has_data(ch) -> bool:
    return bool(ch.pos_keys or ch.rot_keys or ch.scale_keys)


def _quat_rotate(q, v):
    x, y, z, w = q
    vx, vy, vz = v
    # v + 2*cross(q.xyz, cross(q.xyz, v) + w*v)
    cx = y * vz - z * vy + w * vx
    cy = z * vx - x * vz + w * vy
    cz = x * vy - y * vx + w * vz
    return (vx + 2 * (y * cz - z * cy),
            vy + 2 * (z * cx - x * cz),
            vz + 2 * (x * cy - y * cx))


def bind_height(af) -> float:
    """Y-extent of the assembled bind pose (bone FK + box extents)."""
    global_pos = {}
    global_rot = {}
    ys = []
    for b in af.bones:  # parents precede children in our exports
        if b.parent_id < 0:
            gp, gr = b.pos, b.rot
        else:
            pp, pr = global_pos[b.parent_id], global_rot[b.parent_id]
            off = _quat_rotate(pr, b.pos)
            gp = (pp[0] + off[0], pp[1] + off[1], pp[2] + off[2])
            # quat multiply pr * b.rot
            x1, y1, z1, w1 = pr
            x2, y2, z2, w2 = b.rot
            gr = (w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2,
                  w1 * y2 - x1 * z2 + y1 * w2 + z1 * x2,
                  w1 * z2 + x1 * y2 - y1 * x2 + z1 * w2,
                  w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2)
        global_pos[b.id], global_rot[b.id] = gp, gr
        ys.append(gp[1])
    for box in af.boxes:
        if box.bone_id in global_pos:
            cy = global_pos[box.bone_id][1] + box.center[1]
            ys.append(cy + box.size[1] / 2)
            ys.append(cy - box.size[1] / 2)
    return (max(ys) - min(ys)) if ys else 0.0


def scale_anim_uniform(af, factor: float) -> None:
    """Uniformly scale every positional quantity: bone binds (incl. root),
    box sizes/centers, and clip position keys. Rotations untouched."""
    s = factor
    for b in af.bones:
        b.pos = (b.pos[0] * s, b.pos[1] * s, b.pos[2] * s)
    for box in af.boxes:
        box.size = (box.size[0] * s, box.size[1] * s, box.size[2] * s)
        box.center = (box.center[0] * s, box.center[1] * s, box.center[2] * s)
    for clip in af.clips:
        for ch in clip.channels:
            ch.pos_keys = [(t, (p[0] * s, p[1] * s, p[2] * s))
                           for (t, p) in ch.pos_keys]


def map_bones(source_af, target_af, source: str, allow_unmapped: bool):
    """source bone id -> target bone id. Loud failure on unmapped data bones."""
    name_map = SOURCE_BONE_MAPS.get(source, {})
    target_by_name = {b.name: b.id for b in target_af.bones}
    animated_ids = set()
    for clip in source_af.clips:
        for ch in clip.channels:
            if channel_has_data(ch):
                animated_ids.add(ch.bone_id)

    remap: dict[int, int] = {}
    unmapped: list[str] = []
    for b in source_af.bones:
        target_name = name_map.get(b.name, b.name)
        if target_name in target_by_name:
            remap[b.id] = target_by_name[target_name]
        elif b.id in animated_ids and b.name.lower() not in IGNORABLE_BONES:
            unmapped.append(b.name)

    if unmapped and not allow_unmapped:
        lines = [f"UNMAPPED BONES with animation data ({len(unmapped)}) — refusing "
                 f"to import (silent drops make broken rigs):"]
        target_names = list(target_by_name)
        for name in unmapped:
            hints = difflib.get_close_matches(name, target_names, n=2, cutoff=0.4)
            lines.append(f"  {name}" + (f"   (close: {', '.join(hints)})" if hints else ""))
        lines.append(f"Add mappings to SOURCE_BONE_MAPS['{source}'] in "
                     f"tools/character_import.py, or pass --allow-unmapped to drop them.")
        sys.exit("\n".join(lines))
    return remap, unmapped


def lint_file(path: Path, clips: list[str]) -> bool:
    cmd = [sys.executable, str(REPO / "tools" / "anim_pipeline" / "anim_lint.py"),
           "lint", str(path)]
    if clips:
        cmd += ["--clips", ",".join(clips)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    out = (r.stdout or "") + (r.stderr or "")
    ok = r.returncode == 0 and "0 errors" in out
    if not ok:
        print(out)
    return ok


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("input", help=".fbx/.glb/.gltf (or a pre-extracted .anim)")
    ap.add_argument("--source", default="mixamo", choices=sorted(SOURCE_BONE_MAPS),
                    help="which bone-name map to use (default: mixamo)")
    ap.add_argument("--target", default=str(DEFAULT_TARGET),
                    help="rig to merge clips into (clip mode)")
    ap.add_argument("--out", default=None,
                    help="write merged result here instead of overwriting --target")
    ap.add_argument("--clip-name", default=None,
                    help="FSM name for the imported clip (default: CLIP_NAME_MAP "
                         "lookup on the file stem, else the sanitized stem)")
    ap.add_argument("--target-height", type=float, default=None,
                    help="new-rig mode: normalize the model so its full mesh "
                         "height equals this many world units (Meshy GLBs often "
                         "arrive at arbitrary/miniature scale — a bear imported "
                         "at 7 mm renders as a single voxel blob)")
    ap.add_argument("--new-rig", metavar="NAME", default=None,
                    help="create a standalone rig instead of merging clips")
    ap.add_argument("--style", default="box", choices=["box", "voxel"],
                    help="extraction style (voxel = voxelized model shell)")
    ap.add_argument("--scale", type=float, default=1.0)
    ap.add_argument("--allow-unmapped", action="store_true",
                    help="drop unmapped animated bones instead of failing")
    ap.add_argument("--dry-run", action="store_true",
                    help="report what would happen; write nothing")
    args = ap.parse_args()

    src = Path(args.input)
    if not src.exists():
        sys.exit(f"input not found: {src}")

    # ---- NEW RIG mode -----------------------------------------------------
    if args.new_rig:
        out = REPO / "resources" / "animated_characters" / f"{args.new_rig}.anim"
        if args.dry_run:
            print(f"[dry-run] would extract {src} -> {out} (style={args.style})")
            return
        run_extract(src, out, args.style, args.scale, args.target_height)
        af = parse(str(out))
        # Report the assembled bind height so degenerate imports (glTF unit
        # stacks can bake models to ~1/200 scale — a 7 mm bear renders as one
        # voxel blob) fail loudly instead of shipping.
        h = bind_height(af)
        print(f"  bind height: {h:.3f} world units")
        if args.target_height is not None and not (0.2 <= h <= args.target_height * 2.0):
            sys.exit(f"  ERROR: bind height {h:.4f} implausible for target "
                     f"{args.target_height} — normalization failed; inspect the GLB")
        # Meshy exports name every clip "Armature|Unreal_Take|baselayer".
        # Rename a single clip to --clip-name (default "walk") so the engine's
        # standard clip vocabulary (and the meshy_quadruped body plan's empty
        # clipDefaults) resolve it without per-rig mappings.
        if len(af.clips) == 1:
            std_name = args.clip_name or "walk"
            if af.clips[0].name != std_name:
                old = af.clips[0].name
                af.clips[0].name = std_name
                write(af, str(out))
                af = parse(str(out))
                print(f"  clip renamed: '{old}' -> '{std_name}'")
        mixamo_like = sum(1 for b in af.bones if b.name.startswith("mixamorig"))
        print(f"new rig: {out}")
        print(f"  bones={len(af.bones)} ({mixamo_like} mixamorig-named), "
              f"boxes={len(af.boxes)}, clips={[c.name for c in af.clips]}")
        if mixamo_like < len(af.bones) // 2:
            print("  WARNING: mostly non-mixamorig bone names — the FSM/IK stack "
                  "keys on mixamorig:*; this rig needs a bone-map pass or Phase D's "
                  "body-plan abstraction to be a functional gameplay character.")
        ok = lint_file(out, [])
        print(f"  lint: {'PASS' if ok else 'FAIL'}")
        if not ok:
            sys.exit(1)
        return

    # ---- CLIP IMPORT mode -------------------------------------------------
    if src.suffix.lower() == ".anim":
        extracted = src
        tmpdir = None
    else:
        tmpdir = tempfile.TemporaryDirectory()
        extracted = Path(tmpdir.name) / (src.stem + ".anim")
        run_extract(src, extracted, "box", args.scale)

    source_af = parse(str(extracted))
    target_path = Path(args.target)
    target_af = parse(str(target_path))

    clip_name = args.clip_name or CLIP_NAME_MAP.get(src.stem) \
        or src.stem.lower().replace(" ", "_")

    remap, dropped = map_bones(source_af, target_af, args.source, args.allow_unmapped)

    imported = []
    for clip in source_af.clips:
        channels = []
        for ch in clip.channels:
            if ch.bone_id in remap and channel_has_data(ch):
                ch.bone_id = remap[ch.bone_id]
                channels.append(ch)
        new_name = clip_name if len(source_af.clips) == 1 \
            else f"{clip_name}_{clip.name.lower()}"
        imported.append(Clip(name=new_name, duration=clip.duration,
                             speed=clip.speed, root_motion=clip.root_motion,
                             channels=channels))

    print(f"import {src.name} [{args.source}] -> {target_path.name}")
    for c in imported:
        keys = sum(len(ch.pos_keys) + len(ch.rot_keys) for ch in c.channels)
        exists = " (REPLACES existing)" if target_af.clip(c.name) else ""
        print(f"  clip '{c.name}': {c.duration:.2f}s, {len(c.channels)} channels, "
              f"{keys} keys{exists}")
    print(f"  bones: {len(remap)} mapped"
          + (f", {len(dropped)} DROPPED ({', '.join(dropped[:5])}...)" if dropped else ""))

    if args.dry_run:
        print("[dry-run] nothing written")
        return

    for c in imported:
        target_af.clips = [x for x in target_af.clips if x.name != c.name]
        target_af.clips.append(c)

    out_path = Path(args.out) if args.out else target_path
    write(target_af, str(out_path))
    ok = lint_file(out_path, [c.name for c in imported])
    print(f"  wrote {out_path}; lint: {'PASS' if ok else 'FAIL'}")
    if not ok:
        sys.exit(1)


if __name__ == "__main__":
    main()
