"""voxelize.py — model -> scaled SHELL voxels at the size-rule pitch, under the part budget.

Roadmap R2 decisions 1, 2, 8 (docs/CharacterAnimationRoadmap.md):
  * Meshy convention: +Y up, the model faces +Z. The result has its feet on y = 0 and is centred
    on x = 0, z = 0.
  * Scale: the model's own extent (height for bipeds, longest dimension for everything else; 1 u ≈
    1 m) is kept when it already sits inside its D&D size band, otherwise clamped into the band.
    An explicit height_u is clamped the same way. Every clamp is reported in `steps`.
  * Pitch: the size's preferred pitch, then coarser world-grid fractions (1/18 -> 1/9 -> 2/9)
    until the shell fits the part budget — the budget wins, and each step down is reported.
    Reason at the clamp: every character shares ONE 262,144-part instance buffer and overflow makes
    characters invisible (RenderCoordinator::kCharacterInstanceCapacity; MEM Character Instance Buffer).
  * Shell only: surface voxelization; interior voxels are never visible.
  * Colour: each voxel samples the base-colour texture at the nearest surface point (or the
    material/vertex colour when untextured).
Deterministic: same file + arguments -> identical arrays.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path

import numpy as np
import trimesh

CAPACITY = 262144   # RenderCoordinator::kCharacterInstanceCapacity
PITCHES = [1 / 18, 1 / 9, 2 / 9]

# band: height (bipeds) or length (everything else) in world units, 1 u ≈ 1 m (D&D size ranges).
# Medium budget is 2,500 (the gate's 3,000 failed its own crowd check: 40 Medium + 10 Large +
# 2 Gargantuan must fit 75 % of the buffer; the humanoid is 1,116 parts, so 2,500 has headroom).
SIZE_TABLE = {
    "Tiny":       {"band": (0.3, 0.6),  "pitch": 1 / 18, "budget": 600},
    "Small":      {"band": (0.6, 1.2),  "pitch": 1 / 18, "budget": 1500},
    "Medium":     {"band": (1.2, 2.4),  "pitch": 1 / 18, "budget": 2500},
    "Large":      {"band": (2.4, 4.9),  "pitch": 1 / 18, "budget": 6000},
    "Huge":       {"band": (4.9, 9.8),  "pitch": 1 / 9,  "budget": 8000},
    "Gargantuan": {"band": (9.8, 20.0), "pitch": 2 / 9,  "budget": 12000},
}


@dataclass
class Voxels:
    pitch: float
    centers: np.ndarray          # (N, 3) model space, feet on y = 0, facing +Z
    colors: np.ndarray           # (N, 3) uint8
    extent_u: float              # height (biped) or length (creature) after scaling
    size: str
    body_kind: str
    steps: list = field(default_factory=list)


def crowd_parts(crowd: dict) -> tuple[int, int]:
    """Worst-case parts for a crowd {size: count} at each size's budget, and the buffer capacity."""
    return sum(SIZE_TABLE[s]["budget"] * n for s, n in crowd.items()), CAPACITY


def load_mesh(path: Path) -> trimesh.Trimesh:
    mesh = trimesh.load(str(path), force="mesh", process=False)
    if not isinstance(mesh, trimesh.Trimesh) or len(mesh.faces) == 0:
        raise ValueError(f"{path}: no triangle mesh")
    return mesh


def _extent(mesh: trimesh.Trimesh, body_kind: str) -> float:
    ext = mesh.extents
    return float(ext[1]) if body_kind == "biped" else float(ext.max())


def _closest_surface(mesh: trimesh.Trimesh, points: np.ndarray, k: int = 32):
    """Nearest surface point + triangle per point, without trimesh's rtree dependency: a KD-tree
    over triangle centroids proposes k candidates, the exact point-triangle test picks the best."""
    from scipy.spatial import cKDTree
    tris = mesh.triangles
    k = min(k, len(tris))
    _, cand = cKDTree(tris.mean(axis=1)).query(points, k=k)
    cand = np.asarray(cand).reshape(len(points), k)
    flat_tris = tris[cand.reshape(-1)]
    flat_pts = np.repeat(points, k, axis=0)
    cp = trimesh.triangles.closest_point(flat_tris, flat_pts).reshape(len(points), k, 3)
    d = np.linalg.norm(cp - points[:, None, :], axis=2)
    best = d.argmin(axis=1)
    rows = np.arange(len(points))
    return cp[rows, best], cand[rows, best]


def _sample_colors(mesh: trimesh.Trimesh, points: np.ndarray) -> np.ndarray:
    closest, tri = _closest_surface(mesh, points)
    vis = mesh.visual
    if isinstance(vis, trimesh.visual.TextureVisuals) and vis.uv is not None:
        img = getattr(vis.material, "baseColorTexture", None) or getattr(vis.material, "image", None)
        if img is not None:
            img = np.asarray(img.convert("RGB"))
            h, w = img.shape[:2]
            faces = mesh.faces[tri]
            bary = trimesh.triangles.points_to_barycentric(mesh.triangles[tri], closest)
            uv = np.einsum("ij,ijk->ik", bary, vis.uv[faces])
            px = np.clip((np.mod(uv[:, 0], 1.0) * w).astype(int), 0, w - 1)
            py = np.clip(((1.0 - np.mod(uv[:, 1], 1.0)) * h).astype(int), 0, h - 1)
            return img[py, px].astype(np.uint8)
        base = getattr(vis.material, "baseColorFactor", None)
        if base is not None:
            return np.tile(np.asarray(base[:3], dtype=np.uint8), (len(points), 1))
    if hasattr(vis, "face_colors") and vis.face_colors is not None:
        return np.asarray(vis.face_colors)[tri, :3].astype(np.uint8)
    return np.full((len(points), 3), 180, dtype=np.uint8)


def voxelize(path, size: str, body_kind: str, height_u: float | None = None) -> Voxels:
    if size not in SIZE_TABLE:
        raise ValueError(f"size must be one of {list(SIZE_TABLE)}")
    if body_kind not in ("biped", "creature"):
        raise ValueError("body_kind must be 'biped' or 'creature'")
    spec = SIZE_TABLE[size]
    lo, hi = spec["band"]
    mesh = load_mesh(Path(path)).copy()
    steps = []
    natural = _extent(mesh, body_kind)
    want = natural if height_u is None else float(height_u)
    target = min(max(want, lo), hi)
    if abs(target - want) > 1e-9:
        steps.append(f"extent {want:.3f} u clamped to {target:.3f} u ({size} band {lo}-{hi})")
    mesh.apply_scale(target / natural)
    b = mesh.bounds
    mesh.apply_translation([-(b[0][0] + b[1][0]) / 2, -b[0][1], -(b[0][2] + b[1][2]) / 2])

    start = PITCHES.index(spec["pitch"])
    for pitch in PITCHES[start:]:
        grid = mesh.voxelized(pitch)                     # surface voxelization = the shell
        centers = np.asarray(grid.points, dtype=np.float64)
        if len(centers) <= spec["budget"]:
            break
        steps.append(f"pitch 1/{round(1 / pitch)} gives {len(centers)} parts > {size} budget "
                     f"{spec['budget']}: stepping coarser")
    else:
        raise ValueError(f"no pitch fits the {size} budget ({spec['budget']}): {len(centers)} parts at 2/9")
    # deterministic order + feet on the ground: the lowest voxel's bottom face sits on y = 0
    order = np.lexsort((centers[:, 2], centers[:, 0], centers[:, 1]))
    centers = centers[order]
    centers[:, 1] += pitch / 2 - centers[:, 1].min()
    colors = _sample_colors(mesh, centers)
    return Voxels(pitch=pitch, centers=centers, colors=colors, extent_u=target, size=size,
                  body_kind=body_kind, steps=steps)
