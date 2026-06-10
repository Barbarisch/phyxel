"""Generate an 'axis_probe' clip: rotates single bones +60 deg about each local
axis in sequence, on top of the idle stance. Seek to each segment and
screenshot to learn what each axis actually does in the idle-relative frame.

Segments (duration 4.8s, each pose held 0.5s starting at i*0.8):
  0: RightArm +60 X      (seek 0.10)
  1: RightArm +60 Y      (seek 0.27)
  2: RightArm +60 Z      (seek 0.44)
  3: RightForeArm +60 X  (seek 0.60)
  4: RightForeArm +60 Y  (seek 0.77)
  5: RightForeArm +60 Z  (seek 0.94)
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from anim_format import parse, write  # noqa: E402
from pose_dsl import BaseStance, build_clip  # noqa: E402

REPO = Path(__file__).resolve().parents[2]
SOURCE = REPO / "resources" / "animated_characters" / "humanoid.anim"
OUT = REPO / "resources" / "animated_characters" / "humanoid_casts.anim"

SEGMENTS = [
    ("RightArm", (60, 0, 0)),
    ("RightArm", (0, 60, 0)),
    ("RightArm", (0, 0, 60)),
    ("RightForeArm", (60, 0, 0)),
    ("RightForeArm", (0, 60, 0)),
    ("RightForeArm", (0, 0, 60)),
]


def main():
    af = parse(OUT if OUT.exists() else SOURCE)
    stance = BaseStance(af, "idle", 0.0)

    keys = [(0.0, {})]
    for i, (bone, euler) in enumerate(SEGMENTS):
        t = i * 0.8
        keys.append((t + 0.2, {bone: euler}))
        keys.append((t + 0.7, {bone: euler}))
        keys.append((t + 0.8, {}))
    duration = len(SEGMENTS) * 0.8

    clip = build_clip(stance, "axis_probe", duration, keys, {}, subdivisions=0)
    af.set_clip(clip)
    write(af, OUT)
    print(f"wrote axis_probe ({duration}s) into {OUT}")
    for i, (bone, euler) in enumerate(SEGMENTS):
        print(f"  seek {(i * 0.8 + 0.45) / duration:.3f} -> {bone} {euler}")


if __name__ == "__main__":
    main()
