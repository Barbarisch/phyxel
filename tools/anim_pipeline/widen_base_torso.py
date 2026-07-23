"""Rebalance humanoid.anim body breadth — the base rig was voxelized from a
female Mixamo model and read narrow through the chest AND correspondingly
slight through the hips/legs.

ALWAYS applies to the pristine pre-widen baseline (restored from git) so the
scales never compound across runs. Rules (X scales centers+sizes so the voxel
grid stays contiguous; Z scales sizes only — the boxes form a surface shell
whose centers are not symmetric about the bone):

  Spine2 + shoulders:  X x1.14, Zsize x1.06   (upper chest, broadest)
  Spine1:              X x1.08, Zsize x1.05   (mid torso, taper)
  Hips:                X x1.10, Zsize x1.05   (seat/pelvis mass)
  UpLeg + Leg:         X x1.07, Zsize x1.07   (thigh/calf thickness; length untouched)

Joints and animation channels untouched — boxes only, zero animation risk.
Run: python tools/anim_pipeline/widen_base_torso.py
Then re-derive variant rigs: python tools/anim_pipeline/derive_ogre_rig.py
"""
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from anim_format import parse, write

TARGET = "resources/animated_characters/humanoid.anim"
# Last commit with the pristine (pre-widen) rig.
BASELINE_REF = "e1008b4d~1"

# (name-substring, x_scale, z_size_scale) — first match wins.
RULES = [
    ("spine2",   1.14, 1.06),
    ("shoulder", 1.14, 1.06),
    ("spine1",   1.08, 1.05),
    ("hips",     1.10, 1.05),
    ("upleg",    1.07, 1.07),
    ("leg",      1.07, 1.07),
]

baseline = subprocess.run(
    ["git", "show", f"{BASELINE_REF}:{TARGET}"],
    capture_output=True, text=True, check=True).stdout
Path(TARGET).write_text(baseline, encoding="utf-8", newline="\n")

doc = parse(TARGET)
bones = {b.id: b.name.lower() for b in doc.bones}

changed = {}
for shape in doc.boxes:
    name = bones.get(shape.bone_id, "")
    for key, sx, sz in RULES:
        if key in name:
            cx, cy, cz = shape.center
            w, h, d = shape.size
            shape.center = (cx * sx, cy, cz)
            shape.size = (w * sx, h, d * sz)
            changed[key] = changed.get(key, 0) + 1
            break

write(doc, TARGET)
print(f"widened from pristine baseline: {changed}")

check = parse(TARGET)
assert len(check.boxes) == len(doc.boxes) and len(check.clips) == len(doc.clips)
print(f"round-trip OK: {len(check.boxes)} boxes, {len(check.clips)} clips")
