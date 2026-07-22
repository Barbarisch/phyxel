"""Derive ogre.anim from humanoid.anim — the variant-rig pattern.

A variant rig keeps the base skeleton topology, bone names, and every
animation clip UNCHANGED (drop-in compatible with the whole FSM/IK stack) and
resculpts only the MODEL voxel boxes: feature-level exaggeration that
appearance scalars cannot express (jaw, tusks, oversized hands).

Sculpt rules (all about each box's own bone origin, joints untouched):
  hands+fingers   x/y/z centers+sizes x1.45   (massive mitts)
  forearms        x/z centers+sizes  x1.35    (thick lower arms; y = length)
  feet            x/z centers+sizes  x1.20
  head            all centers+sizes  x1.06    (blockier skull)
  + jaw slab and two tusks appended to the Head bone.

CHARACTER FACING CONVENTION (authoritative, from engine code — see
docs/CoordinateSystem.md "Character Facing"): model-space forward = +Z.
AnimatedVoxelCharacter::getForwardDirection() = (sin yaw, 0, cos yaw) and the
model matrix is translate * rotateY(yaw), so at yaw 0 both face world +Z.
Face features (jaw, tusks, snouts) go at POSITIVE Z. Never determine facing
from screenshots of a patrolling character — it turns at waypoints.

Proportion presets (ogre: belly/hunch/bulk) still apply on top at spawn.
Usage: python tools/anim_pipeline/derive_ogre_rig.py
Output: resources/animated_characters/ogre.anim
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from anim_format import parse, write, BoxShape

SRC = "resources/animated_characters/humanoid.anim"
DST = "resources/animated_characters/ogre.anim"

doc = parse(SRC)
bones = {b.id: b.name.lower() for b in doc.bones}
head_id = next(b.id for b in doc.bones if b.name.lower().endswith("head"))

def scale_box(shape, sx, sy, sz):
    cx, cy, cz = shape.center
    w, h, d = shape.size
    shape.center = (cx * sx, cy * sy, cz * sz)
    shape.size = (w * sx, h * sy, d * sz)

counts = {"hand": 0, "forearm": 0, "foot": 0, "head": 0}
for shape in doc.boxes:
    name = bones.get(shape.bone_id, "")
    if "forearm" in name:
        scale_box(shape, 1.35, 1.0, 1.35); counts["forearm"] += 1
    elif "hand" in name:
        scale_box(shape, 1.45, 1.45, 1.45); counts["hand"] += 1
    elif "foot" in name or "toe" in name:
        scale_box(shape, 1.20, 1.0, 1.20); counts["foot"] += 1
    elif "head" in name:
        scale_box(shape, 1.06, 1.06, 1.06); counts["head"] += 1

# Jaw: a heavy underbite slab across the lower face, protruding forward.
# FACE = +Z (engine convention, see module docstring). Keeps the bone's skin
# color.
doc.boxes.append(BoxShape(head_id, (0.20, 0.07, 0.10), (0.0, -0.06, 0.19)))
# Tusks: two upward ivory columns rising from the jaw corners — explicit box
# color (the optional trailing r g b in the MODEL format) so they contrast
# with whatever skin tone the appearance assigns.
IVORY = (0.97, 0.96, 0.90)
for tx in (-0.075, 0.075):
    doc.boxes.append(BoxShape(head_id, (0.05, 0.13, 0.05), (tx, 0.03, 0.215), color=IVORY))

# Guard: face features MUST sit on the +Z side. A sign mistake here has
# happened repeatedly — fail loudly instead of shipping a backward face.
for box in doc.boxes[-3:]:
    assert box.center[2] > 0, f"face feature at z={box.center[2]} — face is +Z!"

doc.header_comments.append("# variant: ogre (derived from humanoid.anim by tools/anim_pipeline/derive_ogre_rig.py — MODEL boxes only, skeleton+clips identical)")

write(doc, DST)
print(f"sculpted: {counts}, +3 jaw/tusk boxes -> {DST}")

check = parse(DST)
assert len(check.bones) == len(doc.bones)
assert len(check.clips) == len(doc.clips)
assert len(check.boxes) == len(doc.boxes)
print(f"round-trip OK: {len(check.boxes)} boxes, {len(check.clips)} clips, {len(check.bones)} bones")
