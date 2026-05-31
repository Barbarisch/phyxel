# Destruction System — Design

Status: **design + P1 in progress** (2026-05-31). Goal: tactile, satisfying destructible voxels/terrain — cast a spell at a chunk and watch it break apart.

## Core model: energy vs. toughness (a game heuristic, not FEM)

A hit deposits **energy** at a point. Energy reaches each nearby voxel (attenuated by distance and by solid matter in the way). Each voxel has a **toughness** (from material). Per voxel:

- `energy_reaching < toughness` → survive (later: accumulate damage / cracks)
- `energy_reaching ≥ toughness` → break
- the **overkill ratio** `r = energy / toughness` (scaled by material brittleness) decides *how* it breaks:
  - `1 ≤ r < S1` → one intact dynamic cube
  - `S1 ≤ r < S2` → shatter into subcubes (1/3)
  - `r ≥ S2` → shatter into microcubes (1/9, capped)

Brittle materials (glass, stone) → low S1/S2 (powder easily); ductile (wood, dirt) → high S1/S2 (chunky).

## Energy propagation: radial + shielding

```
energy_reaching(voxel) = E · falloff(distance) · exp(−α · solidVoxelsInFront)
falloff(d) = max(0, 1 − d/R)^p          (R = radius, p = sharpness)
solidVoxelsInFront = solid voxels strictly between impact and voxel (ray-march occupancy)
α = per-material absorption
```

This gives, from a single hit, a natural gradient: **dust core → chunky-debris shell → cracked rim**, and thick walls shield what's behind them.

Debris velocity = outward from impact (+jitter, + hit direction), magnitude ∝ excess energy.

## Two representations, kept separate
- **Collision** = the voxel **occupancy grid** (GPU `setOccupied`; per-voxel, O(1) to clear on break — why destruction is cheap).
- **Structure** = per-`Cube` `bonds` (6-dir). Drives *connectivity/support* — used later for structural collapse + welded fragments, NOT for the break decision (toughness drives that).

## Backend facts (verified 2026-05-31)
- GPU AVBD physics is the live, stable, primary path; debris = independent `GpuParticle`s (1 particle ↔ 1 body, contact constraints only). Settles correctly; ~78 FPS @ 800 bodies. Scales to thousands.
- Static→dynamic break (existing single-voxel path): `chunk->removeCube` + spawn dynamic + `updateAfterCubeBreak`. Destruction scales this to a region.
- No weld/bond constraints yet → no coherent multi-voxel rigid fragments (P5 stretch goal). Voxels are independent for now.

## Roadmap
- **P1 — DamageSystem + spell test**: `applyDamage(point, radius, energy, type, direction)`; radial+shielding; per-voxel toughness; overkill→tier shatter; GPU debris; spell impact → applyDamage; test scene. *Milestone: fireball → tiered crater + debris.*
- **P2 — Damage accumulation + material tuning**: per-`Cube` `health`, multi-hit chipping; tune toughness/brittleness/absorption per material. (In-memory; persistence later.)
- **P3 — Structural integrity**: bounded connectivity vs static-anchor field → severed groups fall (independent voxels).
- **P4 — Visual damage + polish + perf**: crack/darken feedback, debris caps, shielding tuning.
- **P5 — Coherent welded fragments (future)**: bonds → GPU weld constraints; chunks topple as one piece.
- Cross-cutting: damage persistence to world DB when cracks must survive reload.

## Decisions locked
radial+shielding propagation · damage accumulation wanted (P2; visual cracks separable/P4) · independent voxels now, welds later · anchor = connected to undisturbed static field · bonds = material→material defaults + per-object override later · `ForceSystem` mouse path dropped · everything destructible by default.
