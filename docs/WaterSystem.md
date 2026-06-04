# Water System — Design

> Status: **design / not yet implemented.** This document is the architecture and
> phased roadmap for adding water to Phyxel. Branch: `feature/water-system`.

## Goals (agreed scope)

- **Role:** all of it, eventually — built on a flow-simulation foundation so ambient
  bodies, destruction-driven flooding, and gameplay (buoyancy/swimming) all layer on.
- **Look:** stylized + reflective — translucent blue, Fresnel, scrolling ripple
  normals, planar reflection, screen-space refraction. Not photoreal; "voxel water
  that behaves."
- **Scale:** up to **oceans** — world-spanning water at a base sea level, with real
  flow simulation only near disturbances.

## The hard part

Voxel water is a tension between **scale**, **fidelity**, **per-frame cost**, and
**integration with destruction** — you can't max all four. The design picks a point
in that space deliberately, matching the representation to what water is *for*.

A naive per-voxel water field for an ocean is a non-starter: you can neither store
nor simulate that many cells, and below sea level the answer is trivially "full."

## Core model: implicit sea level + sparse active CA

A **hybrid** of a free global ocean and a small simulated region:

- **Implicit ocean (free, global).** A per-world `seaLevel` Y. Any open (non-solid)
  voxel below sea level is *implicitly* full water — **no storage, no simulation.**
  The renderer draws a surface wherever open space meets sea level. This is the entire
  ocean and every lake, at ~zero runtime cost.

- **Explicit active regions (sparse, simulated).** Only when something disturbs
  equilibrium — a wall blasted below the waterline, a placed source above it, a body
  splashing — do we allocate a real per-voxel **mass field** for the affected chunks
  and run the cellular-automaton (CA) flow there. When the region settles back to
  equilibrium it folds back into the implicit model and stops costing anything.

- **The seam (load-bearing decision).** The implicit ocean is an **infinite
  reservoir**: active-region edge cells that open onto the ocean are held at sea-level
  mass as a boundary condition. Done right, blasting a hole in a sea wall lets the
  "infinite" ocean pour through a finite local sim. This is the part to prototype
  carefully — mass conservation and no-jitter at the boundary.

### Representation details

- Water lives in a **parallel field**, *not* the solid voxel grid. It changes every
  tick and must not churn the solid mesh rebuild or collision occupancy. Solid voxels
  remain the source of truth for collision; water just reads them.
- Per-chunk water storage is **sparse**: a fully-submerged or fully-dry chunk stores
  nothing (a flag); only chunks deviating from the implicit model carry a mass array.
- Mass per cell is a normalized fill `[0,1]` (`float` or `uint8`).

## Connectivity, holes, and authored water (ponds / lakes / rivers)

Two requirements that the altitude-only implicit model does **not** satisfy on its own:

- **Holes must only fill if water can reach them.** The implicit ocean must be
  **connectivity-gated**, not "everything below sea level is water." A sub-sea cell
  renders/contains water only if it is reachable through open space from the sea (or
  another water body/source). Consequences: a bunker dug into a hillside above sea
  level stays dry; a sealed chamber below sea level stays dry until a wall is breached,
  then it floods. The active CA already gives this for free (water only flows where it
  can reach); the implicit optimization needs a **bounded flood-fill from the water
  boundary** to decide which sub-sea cells are "open to water" — the same pattern as
  the destruction-collapse "connected to the main mass?" check (`collapseUnsupported`).

- **Water bodies can be authored after terrain exists, independent of sea level.**
  - *Pond/lake:* deposit a water volume into a basin (or a source that fills until
    settled); the CA levels it to a flat surface at the basin's height — works above
    sea level (mountain lakes).
  - *River:* a persistent **source** uphill feeding flow + a **drain/sink** (or it
    runs to the sea) downhill.
  - These persist in the world DB as part of the water field.
  - **Requires the per-cell surface renderer** — the flat global sea plane cannot draw
    a water body sitting at a different height than the ocean. This raises the priority
    of per-cell rendering and adds **authoring tools** (place volume / source / drain)
    + persistence to the Phase 2/3 scope.

## Simulation: GPU cellular automaton

Runs on the existing `ComputePipeline` infrastructure (the same machinery
`GpuParticlePhysics` drives — multi-pass dispatch, SSBOs, barriers, active-set/sleep).
A water CA is far simpler than the AVBD particle solver.

- **Mass field SSBO** mirroring the active chunk grid, **ping-pong** (two buffers) to
  avoid read/write races.
- **Per tick (~10–20 Hz; water doesn't need 60 Hz):**
  1. **Gravity outflow** — push mass to the cell below if it has capacity.
  2. **Horizontal flux equalization** — move mass to lower-mass neighbors proportional
     to the difference.
  3. **Clamp/conserve** — outflow per cell capped to its available mass; mass stays
     `≥ 0` and globally conserved.
- **Active set / sleep:** skip settled cells; wake on disturbance (voxel removed,
  neighbor changed, body entered). Mirrors the particle solver's high-water/sleep idea.
- **Render interpolation:** interpolate fill level between ticks (the
  `interpAlpha` trick from `particle_expand.comp`) so the surface doesn't pop.

> **De-risk:** prototype the CA flow rules on **CPU for a single chunk** first to nail
> mass-conservation and stability, *then* port to compute. Debugging flow logic in a
> compute shader directly is painful, and the rules are the most iteration-prone part.

## Rendering: a translucent water surface layer

A water mesh layer alongside the chunk mesh, drawn in the existing transparent pass.

- Per chunk, emit **top quads** for water cells with the top vertex dropped by the
  cell's fill level (sloped surface, not hard steps), plus **side quads** at water/air
  boundaries.
- **Surface shading:** blue Fresnel + scrolling ripple normals to start; then layer in
  **planar reflection** (reuse the existing mirror/reflection pass — a water surface is
  a horizontal reflective plane) and **screen-space refraction** (sample the scene
  color buffer with a normal-driven UV offset), plus depth-based color deepening and
  edge foam.

## Integration with destruction

`DamageSystem` already updates occupancy when a voxel is removed. The same hook marks
the freed cell water-eligible and **wakes neighboring water**, so blasting a hole below
the waterline floods through. This is the showcase moment.

## Phased roadmap

Each phase is independently visible/shippable.

| Phase | Deliverable | Notes |
|-------|-------------|-------|
| **0** | Representation + **static ocean surface** | `seaLevel`, implicit model, per-chunk water surface mesh in the transparent pass (blue + Fresnel + ripples). **No sim yet** — but oceans/lakes look good immediately. Validates the render path first. Lowest risk. |
| **1** | **Stylized surface** | Planar reflection (mirror pass), screen-space refraction, depth color, edge foam. The look payoff. |
| **2** | **CA flow core (GPU)** | Mass field, ping-pong, flow passes, active-set/sleep, implicit↔explicit handoff. Hook `DamageSystem` → wake. "Flood through a blasted hole" lights up here. |
| **3** | **Interactions / gameplay** | Buoyancy + drag on rigid bodies and GPU debris, splash spray (reuse particle solver), swimming/drowning, SQLite persistence of *deviations* from sea level. |
| **4** | **Scale hardening + polish** | Distance LOD (far = static surface, near = sim), caustics, underwater fog/post, sound. |

## Risk register

1. **Implicit↔explicit boundary** — mass conservation and no jitter at the ocean seam.
   Prototype first.
2. **CA stability** — flux rules that conserve mass and don't oscillate ("popcorn
   water"). Outflow clamped to available mass; same discipline as debris settling.
3. **Dynamic-surface rendering** — interpolate fill between ticks to avoid popping /
   z-fighting as the surface moves.

## Out of scope

True pressurized 3D Navier–Stokes fluid at world scale; FFT ocean waves *and*
volumetric flow at full fidelity everywhere; particle-realism for large bodies
(particles are for **splash/spray only**, not bulk water).

## Reusable engine assets

- `ComputePipeline` (`engine/{src,include}/vulkan/ComputePipeline.*`) — drives the CA,
  same as the particle solver.
- Planar reflection / mirror pass — water surface reflections.
- Transparency/alpha blending in `RenderPipeline` + `PostProcessor`.
- Chunk system: dirty tracking, occupancy grids, SQLite persistence — home for the
  sparse water field.
- `GpuParticlePhysics` — splash/spray; also the reference pattern for active-set/sleep
  and tick→render interpolation.
- `DamageSystem` voxel-removal hook — flooding integration.
