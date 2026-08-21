#!/usr/bin/env python3
"""gen_creature — compile an ACS creature spec into a Phyxel .anim voxel rig.

The spec format is anyCreature's ACS JSON (MIT, github.com/Ariescar/anyCreature;
see specs/LICENSE-anyCreature.md): a joint tree resolved from relative offsets,
named chains, swept superellipse volumes with Catmull-Rom profiles, six part
types (curve/spike/membrane/fin/eye/paw), and per-joint keyframe animations.
This tool ports the front half of that compiler and replaces the GLB back half
with a voxelizer emitting per-bone greedy-merged boxes.

Pipeline (ForgePattern): spec validation -> joint/skeleton PLAN -> sweep +
parts RASTERIZE into a 0.05 voxel grid (palette x arcs x gradient x hash
noise colors) -> greedy merge -> EMIT .anim via anim_pipeline/anim_format,
finalized with a ground_ref bone and a measured no-slide walk Speed.

Deterministic: same spec + flags -> byte-identical output. No RNG.

Engine caveats baked in:
  * the engine's anim parse cache never invalidates: after regenerating a
    rig, RESTART the engine before spawning it
  * clips: spec 'move' is emitted as 'walk' so the character FSM finds it
  * every box carries an explicit color; bone names should satisfy
    detectMorphology (quadrupeds: exact Pelvis + Chest, a *Tail*/*Paw* bone)

Examples:
  python tools/creature_forge/gen_creature.py tools/creature_forge/specs/ibex.json \
      --out resources/animated_characters/forge_ibex.anim --target-height 1.05
  python tools/creature_forge/gen_creature.py my_creature.json --out out.anim \
      --voxel-size 0.04 --no-noise --force
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "anim_pipeline"))

import anim_format  # noqa: E402

from creature_forge import checks  # noqa: E402
from creature_forge.emit import Options, compile_spec  # noqa: E402
from creature_forge.skeleton import detect_morphology  # noqa: E402
from creature_forge.spec import SpecError, load_spec  # noqa: E402

_RESOLVER_TRAP_SUBSTRINGS = ("wolf", "spider", "dragon")


def write_body_plan(compiled, out_path: Path, species: str) -> Path | None:
    """Emit a body-plan JSON (clipDefaults + morphology) next to the rig when
    the skeleton reads as a quadruped."""
    import json
    names = [b.name for b in compiled.af.bones]
    if detect_morphology(names) != "quadruped":
        return None
    plan = {
        "id": species,
        "morphology": "quadruped",
        "clipDefaults": {"Idle": "idle", "Walk": "walk"},
    }
    plan_path = out_path.parents[1] / "body_plans" / f"{species}.json"
    if not plan_path.parent.is_dir():
        return None
    plan_path.write_text(json.dumps(plan, indent=2) + "\n", encoding="utf-8")
    return plan_path


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("spec", help="ACS creature spec JSON")
    ap.add_argument("--out", required=True, help="output .anim path")
    ap.add_argument("--voxel-size", type=float, default=0.05,
                    help="voxel edge length in world units (default 0.05, "
                         "the fauna-import precedent)")
    ap.add_argument("--target-height", type=float, default=None,
                    help="uniformly rescale the finished rig to this height")
    ap.add_argument("--samples", type=int, default=24,
                    help="animation resample rate per cycle (default 24)")
    ap.add_argument("--no-noise", action="store_true",
                    help="disable per-voxel color noise")
    ap.add_argument("--no-check", action="store_true",
                    help="skip the validation gates")
    ap.add_argument("--no-body-plan", action="store_true",
                    help="do not emit a resources/body_plans JSON")
    ap.add_argument("--force", action="store_true",
                    help="write the .anim even when checks BLOCK")
    args = ap.parse_args(argv)

    out_path = Path(args.out)
    stem = out_path.stem.lower()
    for trap in _RESOLVER_TRAP_SUBSTRINGS:
        if trap in stem:
            print(f"WARN: output name contains '{trap}' — "
                  "CharacterVisualResolver::morphologyFromAnimFile matches "
                  "filenames by substring; pick a name without it")

    try:
        spec = load_spec(args.spec)
        compiled = compile_spec(spec, Options(
            voxel_size=args.voxel_size,
            target_height=args.target_height,
            samples=args.samples,
            noise=not args.no_noise))
    except SpecError as e:
        print(f"SPEC ERROR: {e}")
        return 1

    findings = [] if args.no_check else checks.run(compiled)
    blocks = [f for f in findings if f.severity == "BLOCK"]
    for f in findings:
        print(f"{f.severity}: [{f.rule}] {f.message}")

    if blocks and not args.force:
        print(f"REFUSED: {len(blocks)} BLOCK finding(s); use --force to write anyway")
        return 1

    out_path.parent.mkdir(parents=True, exist_ok=True)
    anim_format.write(compiled.af, out_path)
    af = compiled.af
    walk = af.clip("walk")
    print(f"wrote {out_path}")
    print(f"  bones: {len(af.bones)}  boxes: {len(af.boxes)}  clips: "
          f"{[c.name for c in af.clips]}")
    print(f"  morphology: {detect_morphology([b.name for b in af.bones])}")
    if walk is not None and walk.speed:
        print(f"  no-slide walkSpeed = {walk.speed:.3f}  "
              "(add to FaunaSpawner::walkSpeedFor for biome fauna)")
    if not args.no_body_plan:
        plan = write_body_plan(compiled, out_path, out_path.stem)
        if plan:
            print(f"  body plan: {plan}")
    print("  NOTE: the engine caches parsed .anim files per path forever — "
          "restart the engine to pick this up")
    return 0


if __name__ == "__main__":
    sys.exit(main())
