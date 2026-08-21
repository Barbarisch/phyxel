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
    """Emit a FULL body-plan JSON (rootBone/legs/segments/clipDefaults) next
    to the rig. Minimal plans are forbidden: planForSkeleton scores them 0,
    rejects them, and lets them shadow the real per-morphology default."""
    import json
    from creature_forge.body_plan import derive_plan
    plan = derive_plan(compiled, species)
    plan_path = out_path.parents[1] / "body_plans" / f"{species}.json"
    if not plan_path.parent.is_dir():
        return None
    plan_path.write_text(json.dumps(plan, indent=2) + "\n", encoding="utf-8")
    return plan_path


def run_one(spec_path, out, *, voxel_size=0.05, target_height=None, samples=24,
            noise=True, check=True, body_plan=True, force=False,
            combat=None) -> int:
    """Compile one spec to `out`. Returns 0 on success, 1 on refusal/error."""
    out_path = Path(out)
    stem = out_path.stem.lower()
    for trap in _RESOLVER_TRAP_SUBSTRINGS:
        if trap in stem and not stem.startswith("forge_"):
            print(f"WARN: output name contains '{trap}' — "
                  "CharacterVisualResolver::morphologyFromAnimFile matches "
                  "filenames by substring; pick a name without it")

    try:
        spec = load_spec(spec_path)
        if combat is not None:
            spec["combat"] = combat
        compiled = compile_spec(spec, Options(
            voxel_size=voxel_size, target_height=target_height,
            samples=samples, noise=noise))
    except SpecError as e:
        print(f"SPEC ERROR ({spec_path}): {e}")
        return 1

    findings = [] if not check else checks.run(compiled)
    blocks = [f for f in findings if f.severity == "BLOCK"]
    for f in findings:
        print(f"{f.severity}: [{f.rule}] {f.message}")

    if blocks and not force:
        print(f"REFUSED {out_path.name}: {len(blocks)} BLOCK finding(s); "
              "use --force to write anyway")
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
        print(f"  no-slide walkSpeed = {walk.speed:.3f}")
    if body_plan:
        plan = write_body_plan(compiled, out_path, out_path.stem)
        if plan:
            print(f"  body plan: {plan}")
    return 0


def run_manifest(manifest_path, only=None, **kw) -> int:
    """Batch mode: compile every entry of a bestiary manifest (a JSON array
    in the tools/tree_library.json house style; entries without an 'id' are
    _comment markers). Any BLOCKed species fails the whole run (exit 1)."""
    import json
    manifest_path = Path(manifest_path)
    entries = [e for e in json.loads(manifest_path.read_text(encoding="utf-8"))
               if "id" in e]
    if only:
        entries = [e for e in entries if e["id"] in only]
        if not entries:
            print(f"no manifest entries match {only}")
            return 1
    print(f"Bestiary: {len(entries)} species")
    failures = 0
    for e in entries:
        print(f"--- {e['id']} ---")
        rc = run_one(
            manifest_path.parent / e["spec"], e["out"],
            voxel_size=e.get("voxel_size", kw.get("voxel_size", 0.05)),
            target_height=e.get("target_height"),
            samples=e.get("samples", kw.get("samples", 24)),
            noise=kw.get("noise", True), check=kw.get("check", True),
            body_plan=kw.get("body_plan", True), force=kw.get("force", False),
            combat=e.get("combat"))
        failures += (rc != 0)
    print(f"done: {len(entries) - failures}/{len(entries)} species ok")
    if failures == 0:
        print("NOTE: the engine caches parsed .anim files per path forever — "
              "restart the engine to pick these up")
    return 1 if failures else 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("spec", nargs="?", help="ACS creature spec JSON")
    ap.add_argument("--out", help="output .anim path (single-spec mode)")
    ap.add_argument("--manifest", help="bestiary manifest JSON — batch mode")
    ap.add_argument("--only", action="append",
                    help="manifest mode: build only this species id (repeatable)")
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

    kw = dict(voxel_size=args.voxel_size, samples=args.samples,
              noise=not args.no_noise, check=not args.no_check,
              body_plan=not args.no_body_plan, force=args.force)

    if args.manifest:
        return run_manifest(args.manifest, only=args.only, **kw)

    if not args.spec or not args.out:
        ap.error("single-spec mode needs <spec> and --out (or use --manifest)")
    rc = run_one(args.spec, args.out,
                 target_height=args.target_height, **kw)
    if rc == 0:
        print("  NOTE: the engine caches parsed .anim files per path forever — "
              "restart the engine to pick this up")
    return rc


if __name__ == "__main__":
    sys.exit(main())
