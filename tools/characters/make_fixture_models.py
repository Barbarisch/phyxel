#!/usr/bin/env python3
"""make_fixture_models.py — synthetic stand-ins for Meshy output (roadmap R2 decision 6).

Tests never spend Meshy credits. Until a real import exists, these two low-poly TEXTURED GLBs
exercise the whole pipeline offline. Both follow Meshy's conventions: +Y up, facing +Z, origin on
the ground, a base-colour texture sampled through UVs.

  quadruped.glb  body, head (+Z), 4 legs, tail (-Z); body/legs brown, head orange, tail dark
  biped.glb      A-pose humanoid: torso, head, arms angled down-out, legs; shirt blue, skin, trousers
  biped_bent.glb same body, elbows bent with the forearms pointing forward

Deterministic (no RNG). Writes tests/fixtures/characters/*.glb.
    python tools/characters/make_fixture_models.py
"""
from __future__ import annotations

import sys
from pathlib import Path

import numpy as np
import trimesh
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "tests" / "fixtures" / "characters"

# 4x1 texture: one texel per colour; a part's vertices all map to its texel centre.
PALETTE = [(120, 84, 52), (214, 120, 40), (40, 34, 30), (60, 90, 170)]   # brown, orange, dark, blue
U = {"brown": 0.125, "orange": 0.375, "dark": 0.625, "blue": 0.875}


def part(extents, center, colour, rot_z_deg=0.0):
    m = trimesh.creation.box(extents=extents)
    if rot_z_deg:
        m.apply_transform(trimesh.transformations.rotation_matrix(np.radians(rot_z_deg), [0, 0, 1]))
    m.apply_translation(center)
    uv = np.tile([U[colour], 0.5], (len(m.vertices), 1))
    return m, uv


def build(parts):
    meshes, uvs = zip(*parts)
    mesh = trimesh.util.concatenate(meshes)
    uv = np.vstack(uvs)
    img = Image.new("RGB", (4, 1))
    for i, c in enumerate(PALETTE):
        img.putpixel((i, 0), c)
    mesh.visual = trimesh.visual.TextureVisuals(uv=uv, image=img)
    return mesh


def quadruped():
    # ~1.6 u long (Medium), legs 0.45, body centre at y 0.7
    p = [part([0.45, 0.4, 1.0], [0, 0.7, 0], "brown"),
         part([0.3, 0.3, 0.35], [0, 0.85, 0.65], "orange"),
         part([0.08, 0.1, 0.4], [0, 0.75, -0.7], "dark")]
    for x in (-0.15, 0.15):
        for z in (-0.35, 0.35):
            p.append(part([0.12, 0.5, 0.12], [x, 0.25, z], "brown"))
    return build(p)


def biped():
    # ~1.8 u tall A-pose: legs 0.85, torso to 1.45, head to 1.8
    p = [part([0.42, 0.6, 0.24], [0, 1.15, 0], "blue"),
         part([0.24, 0.28, 0.24], [0, 1.62, 0], "orange"),
         part([0.15, 0.85, 0.16], [-0.11, 0.425, 0], "dark"),
         part([0.15, 0.85, 0.16], [0.11, 0.425, 0], "dark"),
         part([0.11, 0.62, 0.11], [-0.40, 1.15, 0], "orange", rot_z_deg=-35.0),
         part([0.11, 0.62, 0.11], [0.40, 1.15, 0], "orange", rot_z_deg=35.0)]
    return build(p)


def biped_bent():
    """Elbows bent: upper arms down-and-out, forearms pointing FORWARD (+Z) — the orc's pose
    (2026-10-02) that one straight-arm rotation cannot fit."""
    p = [part([0.42, 0.6, 0.24], [0, 1.15, 0], "blue"),
         part([0.24, 0.28, 0.24], [0, 1.62, 0], "orange"),
         part([0.15, 0.85, 0.16], [-0.11, 0.425, 0], "dark"),
         part([0.15, 0.85, 0.16], [0.11, 0.425, 0], "dark")]
    for sx in (-1, 1):
        # upper arm from the shoulder (x 0.25, y 1.40) down-out to the elbow (x 0.40, y 1.12)
        p.append(part([0.11, 0.34, 0.11], [sx * 0.325, 1.26, 0], "orange", rot_z_deg=sx * 28.0))
        # forearm from the elbow forward to the hand (z 0.0 -> 0.32)
        p.append(part([0.10, 0.10, 0.34], [sx * 0.40, 1.12, 0.17], "orange"))
    return build(p)


def main() -> int:
    OUT.mkdir(parents=True, exist_ok=True)
    for name, mesh in (("quadruped", quadruped()), ("biped", biped()), ("biped_bent", biped_bent())):
        path = OUT / f"{name}.glb"
        path.write_bytes(trimesh.exchange.gltf.export_glb(mesh))
        print(f"wrote {path.relative_to(ROOT)} ({path.stat().st_size} bytes, {len(mesh.faces)} faces)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
