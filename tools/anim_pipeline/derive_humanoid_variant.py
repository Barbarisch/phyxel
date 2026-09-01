#!/usr/bin/env python3
"""Derive humanoid monster variants from humanoid.anim — manifest-driven.

Generalizes derive_ogre_rig.py (the proven variant-rig pattern): a variant
keeps the base skeleton topology, bone names, all 84 clips and every
# clip_meta line UNCHANGED — drop-in compatible with the entire melee/IK/
stagger/death stack — and resculpts only the MODEL boxes. Skin/clothing
colors stay with the spawn-time appearance system (colorless boxes take the
appearance region palette); explicit box colors are reserved for features
that must not recolor (tusks, bone, gore).

Manifest: tools/anim_pipeline/humanoid_variants.json — a JSON array
(tree_library.json house style; entries without "id" are _comment markers):

  {"id": "goblin", "out": "resources/animated_characters/goblin.anim",
   "boxScales":  {"<bone-substring>": [sx, sy, sz], ...},
   "boxColors":  {"<bone-substring>": [r, g, b], ...},   # explicit, optional
   "extras": [{"bone": "<bone-substring>", "size": [x,y,z],
               "center": [x,y,z], "color": [r,g,b]?, "mirror_x": true?}]}

CHARACTER FACING: model-space forward = +Z (docs/CoordinateSystem.md).
Face features (jaws, tusks, noses) go at POSITIVE Z center — asserted.

Usage:
  python tools/anim_pipeline/derive_humanoid_variant.py            # all
  python tools/anim_pipeline/derive_humanoid_variant.py goblin ... # subset
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from anim_format import parse, write, BoxShape  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "resources" / "animated_characters" / "humanoid.anim"
MANIFEST = Path(__file__).parent / "humanoid_variants.json"


def sculpt(doc, entry):
    bones_by_id = {b.id: b.name.lower() for b in doc.bones}

    def bone_id_for(sub):
        matches = [b.id for b in doc.bones if sub.lower() in b.name.lower()]
        if not matches:
            raise SystemExit(f"{entry['id']}: no bone matches '{sub}'")
        return matches[0]

    scales = entry.get("boxScales", {})
    colors = entry.get("boxColors", {})
    counts = {}
    for shape in doc.boxes:
        name = bones_by_id.get(shape.bone_id, "")
        for sub, (sx, sy, sz) in scales.items():
            if sub.lower() in name:
                cx, cy, cz = shape.center
                w, h, d = shape.size
                shape.center = (cx * sx, cy * sy, cz * sz)
                shape.size = (w * sx, h * sy, d * sz)
                counts[sub] = counts.get(sub, 0) + 1
                break
        for sub, rgb in colors.items():
            if sub.lower() in name:
                shape.color = tuple(rgb)
                break

    n_extras = 0
    for ex in entry.get("extras", []):
        bid = bone_id_for(ex["bone"])
        color = tuple(ex["color"]) if "color" in ex else None
        centers = [tuple(ex["center"])]
        if ex.get("mirror_x"):
            c = ex["center"]
            centers.append((-c[0], c[1], c[2]))
        for c in centers:
            doc.boxes.append(BoxShape(bid, tuple(ex["size"]), c, color=color))
            n_extras += 1
            if ex.get("face_feature", True) and abs(c[2]) > 0.05:
                assert c[2] > 0, (
                    f"{entry['id']}: face feature at z={c[2]} — face is +Z! "
                    "(set face_feature: false for deliberate back-side boxes)")
    return counts, n_extras


def main(argv=None) -> int:
    only = set(argv or sys.argv[1:])
    entries = [e for e in json.loads(MANIFEST.read_text(encoding="utf-8"))
               if "id" in e]
    if only:
        entries = [e for e in entries if e["id"] in only]
    for entry in entries:
        doc = parse(SRC)
        n_bones, n_clips = len(doc.bones), len(doc.clips)
        counts, n_extras = sculpt(doc, entry)
        doc.header_comments.append(
            f"# variant: {entry['id']} (derived from humanoid.anim by "
            "tools/anim_pipeline/derive_humanoid_variant.py — MODEL boxes "
            "only, skeleton+clips identical)")
        dst = ROOT / entry["out"]
        write(doc, dst)
        check = parse(dst)
        assert len(check.bones) == n_bones and len(check.clips) == n_clips
        assert check.clip_meta("boxing") is not None, \
            "combat clip_meta lines must survive the derivation"
        print(f"{entry['id']}: sculpted {counts}, +{n_extras} extras -> {dst}")
    print(f"{len(entries)} variant(s) done. Engine anim cache is permanent — "
          "restart the engine to pick these up.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
