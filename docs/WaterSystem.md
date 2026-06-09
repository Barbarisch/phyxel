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

## Massive-scale water: infinite bodies, sources/sinks, and flow bounding

Tracking per-cell mass for an ocean (or a continent-spanning river network) is pointless
and unaffordable. The model splits water into two classes:

- **Implicit / infinite bodies** (oceans, large lakes, designer-tagged "infinite
  source" volumes): **not mass-tracked**. Represented by a *level* + a *tag*. They cost
  ~nothing and are rendered cheaply (sea plane / connectivity-gated implicit fill).
- **Active / tracked** water: the mass-conserving CA, run **only** in a bounded region
  near disturbances and along source-fed channels.

### Sources AND sinks (both required)

An infinite **source** with no **sink** floods the world without bound. Source-fed flow
(rivers, springs, waterfalls) only reaches a believable steady state if water has
somewhere to go:

- An implicit body is a **bidirectional boundary**: pinning its edge cells to its level
  each step makes it *supply* water where the area is below the line and *absorb* water
  where above. (Our source primitive — `WaterSimulation::setSource`, pin-to-value —
  already does this; "a river runs into the sea and disappears" is the same mechanism.)
- **Evaporation / distance-decay** is the sink for the thin leading edge of a free flow
  (see below). **Off by default** (`WaterManager` ctor): the default behaviour is that
  water keeps flowing and total volume is conserved — draining one crater into another
  preserves mass. Evaporation is an explicit opt-in (`WaterManager::setEvaporation(true)`)
  for levels that want bounded spread / drying shorelines; with it off there is no decay
  sink, so a free spill on flat ground spreads into a thin film rather than stopping.

So a river is: **source (head, pinned level) → tracked CA flow downhill → sink (ocean
absorbs + decay trims the spread)**. Inflow ≈ outflow → a stable channel; the tracked
region is just the channel + a margin.

### Flow-distance bounding ("water doesn't travel forever")

Free-flowing water carries a **reach budget** that falls off with distance from its
source; at zero it stops. This is the Minecraft 7-block rule, and it does double duty:
believable behavior (a spring doesn't flood a continent) **and** it bounds the active
set (simulated cells stay near sources/disturbances) — the core scale mechanism.

Design fork (decide when building):
- **Level/distance-based**: a per-cell integer "reach" decremented per step. Simple,
  trivially bounded, blockier/less physical.
- **Mass + evaporation**: keep the continuous depth, add a small per-tick decay. More
  natural (variable depth, real pooling), fuzzier bound, needs tuning.
- **Hybrid** (likely best): continuous mass for depth/pooling **+** a source-distance
  "reach" that hard-caps how far a thin sheet spreads.

### Channel / flow-resistance tag (authored riverbeds)

A **per-voxel tag** that exempts marked cells from the distance-decay, so a designer can
paint a riverbed and guarantee water flows its full length from an infinite source,
while un-marked terrain still bounds ad-hoc spills. It's a deliberate **authoring
override on the bounding heuristic**, not a physical force.

- **Authoring:** tag the **riverbed voxels** (a "Channel"/"Riverbed" material); the open
  cell *above* a channel voxel inherits "no-decay" status. The sim reads the tag like it
  reads solidity. Stored per-voxel, persisted in the world DB.
- **Generalize** the binary to a **flow-resistance scalar** per material: channel = 0
  (no decay), normal ground = medium, sand/rough = high (water sinks fast). Binary
  "conductive or not" is the 0-vs-default case.
- **Caveats:**
  1. Still needs a **sink** at the end (sea / pool / evaporation) — exempting decay
     means it won't fade, so it must drain somewhere or pile up.
  2. **Gravity/head still rules** — the tag removes the *distance* limit, not physics.
     Water flows downhill, or uphill only if pushed by a source whose surface sits
     higher than the channel's high point (communicating-vessels pressure). An uphill
     channel past the source level correctly won't flow.
  3. The whole channel stays **active** while sourced — the active region spans its
     authored length (bounded by the designer, not runaway).

### The composite authored river (the payoff)

> Tag a lake as an **infinite source** → paint a **channel** riverbed down to the coast
> → the **sea** (also infinite) is the **sink**.

Water flows the whole authored length, falls as waterfalls where the bed drops, pools
where it widens, and is absorbed at the sea — and the engine simulates **neither ocean**,
only the bounded channel. This is how "oceans + all of it" stays tractable: oceans
implicit, rivers authored + bounded, destruction-water dynamic + decaying.

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

## Future polish ideas

- **Waterfall mist / spray.** Where water drops over a ledge (a side-face skirt with a
  tall exposed bottom), emit a soft fog volume or a GPU particle spray (reuse the
  `GpuParticlePhysics` splash pattern) to sell the cascade. Stylized, cheap, high impact.
- **Sub-voxel terrain (subcubes / microcubes).** The CA is full-voxel (`setSolid` per
  voxel), so partial voxels currently collapse to all-solid/all-empty. If wanted, prefer a
  **per-cell fractional floor height** derived from sub-occupancy (one float/cell, no
  change to cell count → cheap) over true sub-voxel water cells (27×/729× cells —
  infeasible).

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
