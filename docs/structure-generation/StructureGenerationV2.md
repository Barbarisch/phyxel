# Structure Generation v2 — Settlements, Parcels & Ground-Up Buildings

> **This is the canonical design for structure generation.** It supersedes
> [`StructureGenerationPipeline.md`](StructureGenerationPipeline.md) (kept for history). The live
> capability-gap logs — [`StructurePipelineGaps.md`](StructurePipelineGaps.md) and
> [`MaterialTextureNeeds.md`](../MaterialTextureNeeds.md) — stay current and apply to v2.

## Why a rewrite

The v1 pipeline (`tools/structure_pipeline/`) is a decent skeleton, but its realizer makes one
fatal choice — quoting `realize.py`: *"Walls stay full cubes; only the framed openings and roof
coping spend subcubes/microcubes."* Every wall is a 1 m-thick solid cube column, which is the root
of the "Minecraft-y" look. It also bakes a single terrain-agnostic `.voxel` template offline and
patches it onto the ground afterward, so it can never grow a basement, step a footing into a slope,
or reason about a yard, a fence, a path, or a neighbour.

v2 is a **greenfield rewrite** with four locked decisions:

1. **Realizer lives in-engine (C++), terrain-aware.** It queries live terrain *while* building, so
   footings step into slopes, basements excavate to real depth, and doors/navgrid/physics wire up
   directly.
2. **Wall/assembly thickness is configurable per style and material** (a `StyleProfile`), not a
   hardcoded constant.
3. **Prove it with one full-fidelity cottage *on its full parcel*** (fenced yard + entry path to a
   road stub + a yard prop) before scaling to building types or settlements.
4. **Settlement scale (towns/cities) is the north star** — the architecture must not preclude it.

## Core principle: the same problem at every scale

A structure is **assembled, not stamped** — built ground-up in ordered, validated layers, each layer
querying live terrain and the layers beneath it. And the *defining discipline recurses at every
scale*:

> **Claim a footprint → adapt it to terrain → guarantee it doesn't overlap its neighbours →
> guarantee you can walk through it.**

That loop is identical from a microcube in a wall, to a wall assembly in a building, to a building on
a lot, to a lot in a block, to a block in a settlement. v2 builds one set of mechanisms (footprint
claim + occupancy grid + grade adaptation + walkable flood-fill) and **reuses them at each tier.**

## Representation stack (5 levels, top-down)

| # | Level | Authored by | What it is |
|---|-------|-------------|------------|
| 1 | **SettlementPlan** | procedural (+ optional LLM intent) | Road/path network, zones, lot subdivision, per-lot building assignment. Towns & cities. |
| 2 | **ParcelPlan / Lot** | deterministic | One structure's **entire footprint**: building + setbacks + front/back yard + fence ring + entry path to the road + yard props. *This is what gets placed and validated against terrain and neighbours — not the bare building.* |
| 3 | **BuildingProgram** | **LLM** (semantic) | Function, footprint, rooms (purpose + adjacency), stories, substructure choice (slab / crawlspace / basement), roof type, **style id**. *What the building is.* The LLM only ever touches this. |
| 4 | **AssemblyPlan** | deterministic (C++) | Resolved physical anatomy: foundation footprint + terrain-adaptive depth profile, wall assemblies (thickness/material/layers), floor/ceiling assemblies, roof surfaces over the real outline, openings + frames, interior finish runs, fixtures, lights. *How it's built.* |
| 5 | **MicroCanvas realization** | deterministic (C++) | Each assembly paints into the 9×9×9-per-cube grid; greedy coarsening emits bulk→cube, slabs→subcube, ornament→micro. Where "no detail too small" lives. |

The LLM authors **only level 3** (and optionally high-level settlement intent). Everything physical —
thickness, foundations, roofs, geometry — is deterministic and guaranteed by code.

## StyleProfile — `resources/structure_styles.json`

Data-driven, same pattern as `resources/biomes.json`. Per style (`timber_cottage`, `stone_manor`,
`gothic_church`, …):

- **Assembly thickness per type** — exterior wall / interior partition / foundation wall (this is
  where "configurable per style/material" lives).
- **Materials per layer** — structure, cladding, trim, floor, roof.
- **Trim rules** — baseboard height, casing width, quoins, mullions, wainscot.
- **Roof** — pitch, overhang, ridge/fascia/soffit treatment.
- **Ceiling targets** (humble vs grand) and **foundation strategy** (slab / crawlspace / basement
  preference).

## MicroCanvas + the performance rule (decisive)

The MicroCanvas is a C++ port of v1's `DetailCanvas`: paint into a microcube grid (9³ per cube),
then greedily coarsen each region to the largest uniform single-material voxel. The exporter's
coarsening behaviour **dictates the wall strategy**:

- A **0.33 m (subcube-thick) wall coarsens cleanly** — a full-height 1/3-thick wall is exactly **9
  subcubes per linear cube**. Cheap, clean, and dramatically better than a 1 m cube wall.
- A **0.22 m (2-micro) wall does *not* sit on a subcube boundary** → it stays raw microcubes at
  **162 per linear cube**. Far heavier on render and physics.

**Therefore: the default assembly is subcube-thick; microcubes are reserved for trim and relief**
(baseboards, casings, mullions, quoins, carving, bevels). A style may opt into thinner walls but
**pays a measured cost**. The realizer reports realized C/S/M counts and **warns past a per-structure
voxel budget**.

After every build the three "every-load" invariants from `AgentContext.md` MUST run or things fall
through the world: rebuild **CPU occupancy** (`buildAllChunkPhysics`), **GPU occupancy**
(`rebuildOccupancyFromChunks`), and the **navgrid** (new API — see Engine work).

## Building construction — ordered, validated passes (all terrain/layer-aware)

| # | Pass | Notes |
|---|------|-------|
| 0 | **Site & grade** | Sample terrain occupancy under the footprint → grade plane + slope; decide cut/fill. (Extends v1 `seatStructure`'s median-grade logic.) |
| 1 | **Substructure** | Footings to bearing, **stepped on slope**; foundation walls; then slab-on-grade / crawlspace void / excavated basement (+ stair down). |
| 2 | **Floor system** | Subfloor + finish floor per room (`floor_mat`). |
| 3 | **Superstructure shell** | Exterior wall assemblies (subcube-thick per style) + thinner interior partitions, at canon story heights. |
| 4 | **Inter-story floors / ceilings** | Stairwell holes carved **after** all floors exist (the v1 refill bug, fixed by construction). |
| 5 | **Roof** | Hip/gable/valley over the **union outline** (fixes the v1 L/T/U gap) + attic void + eave overhang + fascia/soffit + ridge. |
| 6 | **Openings** | Cut doors/windows; build reveals, sills, lintels, jambs, frames; `open / glass / shutter / boarded` infill. |
| 7 | **Interior finish** | Baseboards, door casings, wainscot, window trim (micro relief). |
| 8 | **Fixtures & furniture** | Against the correct wall, on the finish floor (no float/clip), facing the room. |
| 9 | **Lighting** | Lamp props + co-located point lights; daylight accounting per room. |
| 10 | **Functional wiring** | Register doors (`DoorManager`), emit room nav markers, rebuild navgrid + CPU + GPU occupancy. |

## Component & asset generation — the trusted library

Furniture, fences, gardens, gates, and props are **level-5 components**: the smallest tier of the same
spine (claim a footprint → don't float → be usable → measured against a ruler). They get their own
spec, their own gates, and — critically — their **own trusted library with status**, so a mediocre
asset can never silently propagate into everything. We never trust an LLM's self-assessment of its own
asset.

### DimensionCanon — `resources/object_dimensions.json`
The objective ruler for objects, a sibling of `character_design_constraints.json`. Each **archetype**
carries real-world canonical dimensions (converted to cubes; 1 cube ≈ 1 m) + tolerance + structural
rules + required anchors. Examples:

```jsonc
"fence_picket":  { "height": 0.9,  "tol": 0.15, "post_spacing": 1.8, "rails": 2, "gate_min_w": 1.0 },
"fence_privacy": { "height": 1.8,  "tol": 0.15, "solid": true,        "gate_min_w": 1.0 },
"chair_dining":  { "seat_top": 0.45, "back_top": 0.9, "seat_depth": 0.4, "anchors": ["seat_0"] },
"table_dining":  { "top": 0.75, "tol": 0.05, "leg_inset_min": 0.1, "anchors": ["surface"] },
"door_interior": { "clear_h": 2.0, "clear_w": 0.81 }
```

A "picket fence" that comes out 1.6 m tall **fails before anyone looks at it.** Real-world dimensions
(picket ≈ 0.9 m / 3 ft, privacy ≈ 1.8 m / 6 ft, etc.) are the source of truth, not LLM guesswork.

### AssetValidator — deterministic gates (no LLM in the verdict)
On the realized voxels, mirroring the building geometry gates:
- **Dimensional** — bounding height/width/depth within canon ± tolerance.
- **Structural** — base rests on the floor plane (no float); expected connected-component count (a
  chair is one piece; a fence is posts+rails joined); no overlapping voxels; voxel count within budget.
- **Functional** — required anchors / interaction points present and reachable (a chair's `seat_0`
  resolves a sit; a gate opening ≥ character width).
- **Symmetry / repeat** where the archetype demands it (evenly spaced pickets).

### Aesthetic judging — comparative, never self-graded
Deterministic gates catch wrong-size / floating / broken, not "ugly." For aesthetics, reuse the visual
critique machinery (`critique_template` / `orbit_screenshots` with the **reference character beside the
asset for scale**) as a **separate judge**, and make it **comparative** — score variants against a
rubric and against the current library exemplar, not "rate your own work." Comparison + an external
judge beats LLM self-bias.

### Generation strategy — variants then repair the winner (locked)
For each archetype: generate **N candidate variants in parallel**, rank by gates + comparative visual
rubric, pick the best, then run a **repair loop** on that winner to fix residual gate failures. (Time
is not the constraint; quality is.) Cheaper single-candidate repair is the fallback only when N>1 is
wasteful.

### The trust mechanism — library status, not self-claim (locked)
Every asset is a **library record** with provenance:

```
archetype, target dims, realized dims, gate results, quality score, status, version
```

`status ∈ { provisional, approved, deprecated }`. **The building/parcel realizer may select ONLY
`approved` assets.** Promotion path (locked): a candidate must pass the deterministic gates **and** the
visual critique, **then surface to the user for a one-time yes/no** before it is marked `approved`.
Until then it is `provisional` and quarantined — physically unusable in any structure. So a bad fence
**cannot** propagate.

- If no `approved` asset exists for a needed archetype, the pipeline regenerates until one passes +
  is approved, or logs the gap (`StructurePipelineGaps.md`) and uses a **clearly-marked placeholder** —
  never silently ships junk.
- A **golden regression** per approved asset prevents a re-generation from quietly making it worse:
  a regen must re-pass the gates and be re-approved before it replaces the blessed version.
- Assets are **versioned**; `deprecated` lets us retire something without breaking structures that
  referenced it.

This is methodical by construction: **one archetype at a time**, each measured against the
DimensionCanon, gated, judged comparatively, and human-approved before it becomes reusable.

## Parcel & settlement spine

### ParcelPlan (Lot)
The bounded site for one structure: building footprint + setbacks + front/back yard + **fence ring**
(thin sub-cube assembly: posts + rails / hedge, **gate where the path crosses**) + **entry path**
connecting the front door to the road + **yard props** (front: path + optional garden/well; back:
garden bed, woodpile, shed, clothesline). The yard-prop library also clears the existing "no
courtyard/yard props" gap in `StructurePipelineGaps.md`.

### NetworkGraph (roads & paths, first-class)
Nodes (intersections, building anchors) + edges (segments carrying width / material / class:
highway → street → lane → footpath). Edges **realize as terrain-following graded voxel ribbons**,
**reusing the same grade/cut/fill/excavate helpers as the building substructure pass** (surface
material, subcube curbs, steps/switchbacks on slope).

Two directions of the one graph (both supported; **accretive-first**):
- **Building-driven (accretive / organic village):** a placed building emits a connection stub
  (front door → desired frontage); a path grows to join the network. Produces medieval villages and
  composes directly with the single-building slice.
- **Road-driven (planned town/city):** lay the road graph first → subdivide blocks into lots
  (recursive OBB / straight-skeleton) → stamp buildings oriented to the street.

For true city scale the scalable reference is tensor-field / L-system road networks following
elevation/water (Parish & Müller, *Procedural Modeling of Cities*) — slot at the city phase.

### SettlementOccupancyGrid (the context-awareness arbiter)
A 2D footprint-claim grid (with grade/height), one tier up from the voxel `VoxelOccupancyGrid`.
Every candidate — road segment, lot, building, fence, path — **claims a footprint mask**; the placer
**rejects overlaps and scores candidates by slope, road frontage, and orientation**. This is how
"account for the entire footprint of a structure" becomes literal and context-aware.

## Validation — the "does it function" gates, at every scale

Same flood-fill / footprint discipline, scaled. Blockers feed the LLM repair loop and/or trigger a
deterministic re-placement; soft ergonomics are warnings.

**Building:**
- *Pre-build (no voxels):* topology (rooms tile, no overlap), reachability graph entrance → every
  room *including basement/attic*, scale clearances vs canon (ceiling/door/stair), buildable slope.
- *Post-build geometry (realized voxels):* no overlapping voxels; **foundation reaches bearing
  everywhere** (no floating footing on slope); floors continuous (holes only at stairwells); stair
  per-step headroom; door opening == leaf; roof covers every exposed top column; **fixtures rest on
  the finish floor and don't clip/overlap/block doorways**; under-slope & attic headroom.
- *Walkability (capsule flood-fill):* entrance → every room/story; threshold flush with exterior grade.
- *Runtime (live engine):* spawn the humanoid, pathfind to each room marker, climb stairs, open every
  door (and confirm a locked door blocks). **Depends on the `rebuild_navgrid` engine fix.**

**Parcel / settlement:**
- Footprint **non-overlap** across lots/roads/buildings (the SettlementOccupancyGrid).
- **Town-scale reachability:** every building's front door → its path → the road network → every
  *other* building.
- Road grade navigable; lot terrain-fit within a cut/fill budget; entrances actually face frontage.

## LLM's role (bounded)

The LLM authors the **BuildingProgram** only (massing, room program, adjacencies, style choice) and
optionally high-level **SettlementPlan** intent (this is a market town of ~30 homes around a church).
Schema-constrained + a repair loop against the pre-build validator. It never emits voxels, thickness,
foundations, or roofs — those are deterministic and guaranteed. For **assets** the LLM proposes
candidate variants but **never blesses its own work**: promotion to the reusable library requires the
deterministic gates + comparative visual judge + a one-time user approval (see *Component & asset
generation*).

## Engine work this implies (realizer is C++ now)

- **`StructureRealizer`** subsystem (the layered passes) + **`MicroCanvas`** (C++ greedy-coarsening
  voxel painter, unit-tested).
- **`StyleProfile`** loader (`resources/structure_styles.json`).
- **Terrain grade/excavate/backfill** helpers (extend `PlacedObjectManager::seatStructure`).
- **Hip/gable/valley roof generator** over an arbitrary outline (open gap).
- **`rebuild_navgrid` API** (open gap) — blocks live walk-in verification and Tier-C runtime nav.
- **Door handedness / mirroring** + optional **free-swing physics doors** (open gaps).
- **Wall-mounted / ceiling-hung fixture** placement (open gap).
- **`NetworkGraph`** + road/path realizer; **`SettlementOccupancyGrid`** + placer.
- **Asset library** — `DimensionCanon` loader (`resources/object_dimensions.json`), `AssetValidator`
  (deterministic dimension/structural/functional gates), and a library record store with
  `status` (provisional/approved/deprecated) + version that the realizer queries (`approved`-only).
- **New materials** — plaster/stucco, tile/marble, cloth/linen, roof shingle, plumbing/water — as
  **documented placeholders** logged to `MaterialTextureNeeds.md` (placeholders are fine; the
  deficiency must be recorded).
- MCP/HTTP surface: `build_structure` (program → build), plus inspection tools.

## First vertical slice — the cottage on its parcel

A ~7×9 **timber cottage**, 1 story + crawlspace, gable roof, **4 real rooms** (living/hall, kitchen,
bedroom, small bath/pantry), one lockable front door + windows — **sitting on its full parcel**: a
fenced yard with a gate, an entry path from the front door to a **road stub**, and at least one yard
prop (well or garden bed).

Full fidelity: subcube timber-frame walls with corner posts, finish floor, baseboards + door casings,
framed windows with sills, gable roof with eave overhang + fascia, real foundation + crawlspace vents,
furnished + lit.

**Acceptance:** builds on flat **and** sloped terrain; all geometry + walkable gates pass; the
SettlementOccupancyGrid shows building/yard/fence/path/road non-overlapping; and a **live character
walks from the road stub, up the path, through the gate, in the front door, and through every room**,
with the navgrid confirming reachability. One artifact exercises every building layer, the parcel
tier, the first NetworkGraph edge, and every gate.

## Phasing

**Asset track (parallel + foundational — feeds P4 furnishing and P5 parcel fences)**
- **A0** — `DimensionCanon` (`resources/object_dimensions.json`) + `AssetValidator` (deterministic
  gates) + the library record store with `status`/version; the realizer's `approved`-only selection.
- **A1** — Generation: variants → comparative visual rank → repair the winner → gate → **user
  approval** → `approved`. Golden regression per blessed asset. Seed the cottage's needed archetypes
  (timber-frame parts, picket/privacy fence + gate, kitchen & bath fixtures, well/garden-bed yard
  prop) one archetype at a time.

**Building stack**
- **P0** — `StyleProfile` schema + loader; C++ `MicroCanvas` (port + unit-test coarsening);
  `BuildingProgram` / `AssemblyPlan` types; pre-build validator. *Nothing renders yet.*
- **P1** — Ground-up realizer on **flat** ground (passes 0–5, rectangle, subcube walls) → cottage
  shell; geometry gates.
- **P2** — **Terrain adaptation** (stepped footings, excavation/backfill, entry steps, flush
  threshold) → cottage on a slope; walkable gate + **`rebuild_navgrid`** → live walk-in.
- **P3** — Openings + interior finish (reveals/sills/lintels/frames; baseboards/casings/wainscot;
  glass/shutter infill).
- **P4** — Room programs + furnishing (**kitchen & bath** fixtures incl. plumbing material gaps;
  furnish/clutter/lights passes; fixture-on-floor + no-clip gates).

**Parcel + settlement (the first slice spans P1–P5 because the cottage ships with its parcel)**
- **P5 — Parcel**: `ParcelPlan` (fenced yard + gate + entry path + yard prop) + the first single
  `NetworkGraph` edge (the road stub). **Completes the first-slice acceptance bar.**
- **P6** — LLM `BuildingProgram` author + repair loop on the new validators; runtime playtest harness;
  vision/scale critique.
- **P7** — Building types (shop / church / tavern / manor) via style + program libraries; articulated
  massing (L/T/U), hip/valley roofs, habitable basements/attics, multi-story; persist whole buildings
  as composite templates.
- **P8 — Network & roads**: full `NetworkGraph` + terrain-following road/path realizer + **building-
  driven** path growth (accretive).
- **P9 — Lots & blocks**: `SettlementOccupancyGrid` + lot subdivision + **road-driven** placement
  oriented to frontage. Slice: a **5–10 cottage hamlet around a green**.
- **P10 — Town/city scale**: zoning (residential/market/civic), district road hierarchy
  (tensor-field/L-system), landmark anchors (church/well/square), variety via style + program
  libraries. Slice: a walled **town**. (Settlement *site selection* ties into the biome/
  `WorldGenerator` system here — pick flat-enough, near-water/road land. A hook, not a P8 blocker.)

## Disposition of v1 & gap discipline

- **`tools/structure_pipeline/` (v1) stays in place until v2 reaches feature parity (~P4)** — it is
  the only working generator we have. Borrow ideas (greedy coarsening, the gate philosophy), not the
  code (v2 realizes in C++). Retire v1 once v2 is at parity.
- Every capability we wish for and don't have → log it the moment we hit it:
  engine/realizer gaps → `StructurePipelineGaps.md`; material/texture gaps → `MaterialTextureNeeds.md`.

## References
- Scale canon: `resources/character_design_constraints.json` (character ≈ 1.751 cubes).
- Sub-voxel collision: per-chunk `VoxelOccupancyGrid` in `VoxelDynamicsWorld` (see `AgentContext.md`).
- Terrain seating today: `PlacedObjectManager::seatStructure` (median grade → excavate → entry steps).
- Doors: `DoorManager` (swinging, lockable, persistent, navgrid-aware).
- Procedural city roads (city phase): Parish & Müller, *Procedural Modeling of Cities* (tensor-field /
  L-system networks following terrain).
