"""Author an ADDITIVE carry layer from an existing pose (A3, docs/AnimationSystemV3Plan.md §3b).

A carry layer is a `role=layer` clip the runtime composes over ANY base (walk, run, idle) on the
bones of its mask. Additive layers apply the delta from their own frame 0, so the clip is two
keys per upper-body bone:

    t = 0          the BASE reference pose (default: `idle` at t=0)
    t = duration   the SOURCE pose (e.g. `slash2h_idle` at t=0 — the two-handed ready stance)

delta = inverse(frame0) * frame1 = "how the source differs from idle", which is exactly the
carry offset. Legs, feet and the root are never part of the layer (they stay the base's).

    python tools/anim_pipeline/make_carry_layer.py resources/animated_characters/humanoid.anim \
        --source slash2h_idle --name carry_2h_heavy --grip 2h_heavy --write

Writes the clip and its `# clip_meta:` line (role=layer grip=<grip> mask=upper additive=1
type=layer source=<clip>). Re-running replaces the clip. Verify with
`anim_lint.py metacheck` and the LayerComposition / SwordHammerPair tests.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import math

from anim_format import AnimFile, Channel, Clip, parse, write  # noqa: E402
from anim_lint import qangle as _qangle_rad, sample_rotation  # noqa: E402


def qangle(a, b) -> float:
    """Geodesic angle in DEGREES (anim_lint's helper returns radians)."""
    return math.degrees(_qangle_rad(a, b))

LEG_TOKENS = ("leg", "foot", "toe")


def upper_body_bones(af: AnimFile) -> list:
    """Bones outside the legs and not the skeleton root — the engine's default `upper` mask."""
    out = []
    for b in af.bones:
        if b.parent_id < 0:
            continue
        low = b.name.lower()
        if any(tok in low for tok in LEG_TOKENS):
            continue
        out.append(b)
    return out


def pose_rotations(af: AnimFile, clip: Clip, t: float) -> dict:
    """{bone_id: local rotation (x,y,z,w)} at t; bind rotation where the clip has no channel."""
    rots = {b.id: tuple(b.rot) for b in af.bones}
    for ch in clip.channels:
        if ch.rot_keys:
            rots[ch.bone_id] = tuple(sample_rotation(ch.rot_keys, t))
    return rots


def build_layer(af: AnimFile, source: str, source_time: float, base: str, base_time: float,
                name: str, duration: float, min_delta_deg: float) -> tuple[Clip, dict]:
    src = af.clip(source)
    bse = af.clip(base)
    if src is None:
        raise SystemExit(f"source clip '{source}' not found")
    if bse is None:
        raise SystemExit(f"base clip '{base}' not found")
    src_rot = pose_rotations(af, src, source_time)
    base_rot = pose_rotations(af, bse, base_time)
    clip = Clip(name=name, duration=duration)
    stats = {"bones": 0, "moved": 0, "max_delta_deg": 0.0}
    for b in upper_body_bones(af):
        r0, r1 = base_rot[b.id], src_rot[b.id]
        d = qangle(r0, r1)
        stats["bones"] += 1
        stats["max_delta_deg"] = max(stats["max_delta_deg"], d)
        if d < min_delta_deg:
            continue                       # identity delta: leave the bone to the base
        stats["moved"] += 1
        clip.channels.append(Channel(bone_id=b.id, rot_keys=[(0.0, r0), (duration, r1)]))
    return clip, stats


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("anim")
    ap.add_argument("--source", required=True, help="clip whose pose becomes the carry (e.g. slash2h_idle)")
    ap.add_argument("--source-time", type=float, default=0.0)
    ap.add_argument("--base", default="idle", help="reference pose clip (default idle)")
    ap.add_argument("--base-time", type=float, default=0.0)
    ap.add_argument("--name", required=True, help="layer clip name (e.g. carry_2h_heavy)")
    ap.add_argument("--grip", required=True, help="grip factor the layer matches (schema enum)")
    ap.add_argument("--mask", default="upper")
    ap.add_argument("--additive", type=int, choices=(0, 1), default=1,
                    help="1 = additive delta over the base (a carry OFFSET that keeps the base's arm swing); "
                         "0 = override the masked bones with the source pose (a two-handed carry: both hands "
                         "stay on the shaft, no arm swing — measured live 2026-09-30: the additive 2H carry let "
                         "the walk's arm swing carry the second grip up to 0.19 u out of the off-hand's reach)")
    ap.add_argument("--duration", type=float, default=0.1, help="seconds to reach the pose (layer time clamps)")
    ap.add_argument("--min-delta-deg", type=float, default=0.5, help="skip bones that barely move")
    ap.add_argument("--write", action="store_true")
    args = ap.parse_args(argv)

    af = parse(args.anim)
    clip, stats = build_layer(af, args.source, args.source_time, args.base, args.base_time,
                              args.name, args.duration, args.min_delta_deg)
    print(f"{args.name}: {stats['moved']}/{stats['bones']} upper-body bones carry a delta "
          f"(max {stats['max_delta_deg']:.1f} deg) from {args.source}@{args.source_time:.2f} vs {args.base}@{args.base_time:.2f}")
    if not clip.channels:
        print("nothing to write: the source pose equals the base on every upper-body bone")
        return 1
    if args.write:
        af.set_clip(clip)
        af.set_clip_meta(args.name, {"type": "layer", "role": "layer", "grip": args.grip,
                                     "mask": args.mask, "additive": str(args.additive), "source": args.source})
        write(af, args.anim)
        print(f"wrote {args.name} to {args.anim}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
