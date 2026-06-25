# Structure Generation Pipeline

> **⚠️ SUPERSEDED by [`StructureGenerationV2.md`](StructureGenerationV2.md)** (greenfield rewrite:
> in-engine C++ terrain-aware realizer, sub-cube walls, parcels/yards/fences, and settlement-scale
> town/city generation). This v1 doc is kept for history and describes the `tools/structure_pipeline/`
> Python implementation that remains in place until v2 reaches feature parity (~P4).

> Design doc for functional, multi-resolution **structure, furniture, and item** generation. Goal:
> combine LLM spatial/semantic reasoning with deterministic voxel realization to produce **functional**
> buildings (lockable doors, multiple reachable floors, multi-purpose: home/shop/church/stadium/…),
> plus furniture and items, that exploit Phyxel's cube + subcube + microcube resolution and are
> **scaled to look right next to the humanoid character**, with an automated generate → validate →
> scale-check → visually-confirm loop. Buildings, furniture, and items share one spec/validation/
> confirmation stack (see *Unified scope* and *Scale Canon*).

## Decisions (locked)

1. **Architecture: LLM-as-architect + deterministic realizer.** The LLM emits a validated,
   resolution-independent `BuildingSpec`; deterministic code turns it into voxels, subcube
   detail, and registered functional doors. Functional guarantees come from code, not the model.
2. **Realizer home: extend the C++ `StructureGenerator`** (`engine/src/core/StructureGenerator.cpp`).
   Runs in-engine via `build_structure`, fast, directly wired to `DoorManager` /
   `PlacedObjectManager`. Spec authoring + validation + LLM live in Python (`tools/`).
3. **First vertical slice: House** — 1–2 floors, one lockable front door, a couple of rooms.
   Proves the whole spec → realize → verify loop before adding building types.

## Why this shape

Two generation paths exist today and don't share a representation:

- **Deterministic C++ `StructureGenerator`** — rich primitives + composites (`house`/`tavern`/
  `tower`), subcube detail primitives (`door_frame`, `window_frame`, `railing`, `half_wall`,
  `pitched_roof`, subcube staircases), furniture. JSON-driven (`generateFromJson`), exposed as
  `build_structure` / `list_structure_types`. Composites are hardcoded; **door openings are just
  gaps** (no door object placed).
- **LLM/BlockSmith building path** — `tools/blocksmith_generate.py --building` + `building_prompts.py`
  cuboid DSL → bbmodel → voxel. Reads well to the LLM but **cube-only**, gaps for doors, separate
  from the C++ path.
- **`DoorManager`** — fully functional swinging, **lockable** (`keyItemId`), persistent,
  navgrid-aware doors (MCP `register_door` / `set_door_lock`). **Not wired into generation.**
- **Visual-confirm loop** — `critique_template` / `refine_template` / `generate_asset`: generate →
  multi-angle screenshot → vision critique → regenerate to a quality threshold. Template-scoped.

External reference — **MineBench** (minebench.ai): prompt → voxel coords, or "tool mode" minimal
primitives (block/box/line), then vision/human-judged. Two lessons: **primitive/tool mode beats
raw-coordinate dumps** at scale, and **a judged feedback loop** measures "good enough." MineBench
stops at cubes + aesthetics and has **no concept of function** — that is our differentiator.

## The Building Spec (intermediate representation)

A single resolution-independent, *semantic* representation both sides cooperate on. The LLM
produces it (layout / program / style); deterministic code realizes it (clean geometry, exact
subcube detail, guaranteed-functional doors/stairs). The "primitives" are *functional* (rooms,
portals, stairs), not raw boxes.

```jsonc
BuildingSpec {
  function: "house" | "shop" | "church" | "tavern" | "stadium",
  footprint: { w, d },
  style: "medieval",
  palette: { wall, floor, roof, trim, ... },
  stories: [{
    height: 5,                                   // interior height in cubes
    rooms:    [{ id, rect:[x,z,w,d], purpose, floor_mat }],
    portals:  [{ between:["roomA","exterior"], pos, width, height,
                 kind:"door|arch|window",
                 door:{ lockable:true, key:"brass_key", swing:90 } }],
    stairs:   [{ from_story, to_story, pos, kind:"straight|spiral" }],
    fixtures: [{ type:"altar|counter|pew|bar|bed|table", rect, facing }]
  }],
  roof: { style:"pitched|flat", mat }
}
```

Properties that make this the right contract:

- **Checkable before any voxel exists** (static validation): rooms don't overlap, every room
  reaches the entrance through portals, every story is reachable by stairs, door openings ≥
  character width, ceilings ≥ character height.
- **Both existing paths feed it**: C++ composites become *realizer backends*; the LLM cuboid DSL
  becomes a *spec author*.
- **Functional doors fall out for free**: for each `portal.kind == "door"`, the realizer cuts the
  opening **and** emits a sized door leaf, places it via `PlacedObjectManager`, and calls
  `DoorManager::registerDoor(... lockable / key ...)`.
- **Multi-function = data, not code**: a `room_program` library (data-driven JSON, same pattern as
  `resources/biomes.json`) per function. LLM fills/varies the program; deterministic realizes it.

## Unified scope: buildings, furniture, and items

These are the same problem at three scales — generate → add subcube/microcube detail → **scale to
the character** → visually confirm. They share one discipline: a **ScaledSpec** base where every
dimension is expressed in the shared cube unit (with the humanoid character as the ruler — see Scale
Canon), every asset carries **anchor / interaction points**, and all three run the same validation +
confirmation loop. Only the realizer backend and the meaning of "functional" differ per tier:

| Tier      | "Functional" means                              | Spec shape                                   | Anchors                          |
|-----------|-------------------------------------------------|----------------------------------------------|----------------------------------|
| Building  | Reachable floors, working/lockable doors        | rooms / portals / stairs / fixtures          | door hinges, room markers, spawn |
| Furniture | Usable interaction points sized to the character | dims + interaction points (seat/surface/lie) | seat_0, surface, hinge           |
| Item      | Held/equipped, reads at scale, correct slot     | dims + grip point + equip slot               | grip, tip, equip slot            |

**Furniture *is* the building's `fixtures`.** A `BuildingSpec.fixtures` entry references a furniture
spec instance, so furniture is both standalone-generatable and embeddable in buildings. **Items**
anchor onto furniture surfaces (a mug on a table, a sword on a rack) via anchor points — the same
mechanism. This means one spec/validation/confirmation stack covers all three; the realizer dispatches
on tier. Furniture also already has engine support to reuse: `InteractionProfileManager`,
`SeatInteractionHandler`, the seat subcube recipes in `resources/character_design_constraints.json`,
and the `get_character_design_constraints` / interaction-editor (IE) calibration tools.

## Scale Canon — everything is measured against the character

The single source of truth is `resources/character_design_constraints.json` (re-measured whenever the
`.anim` changes). The default `humanoid_normal` is **1.751 cubes tall** (≈1.75 m, so 1 cube ≈ 1 m).
**Rule: no spec dimension is an absolute literal — it is expressed relative to the character and
validated against these constraints.** Pre-measured anchors to size against:

| Target                | Cubes        | Source                                  |
|-----------------------|--------------|-----------------------------------------|
| Character height      | 1.751        | `character_height` / `head_top`         |
| Hip / lumbar          | 0.868        | `hip_bottom`, `backrest_lumbar_y`       |
| Seat top              | 0.479 ideal / 0.667 subcube-achievable | `seating.seat_top_*` |
| Seat depth / width    | ≥0.389 / ≥0.500 (2 subcubes each) | `seating.seat_depth_min`, `seat_width_min` |
| Chair backrest top    | 1.667 (throne 2.0) | `backrest_top_y_chair`            |
| Table top (standing)  | 1.000        | `tables.standing_height_ideal`          |
| Table top (seated)    | 0.868        | `tables.seated_height_ideal`            |
| **Door clear height** | ≥ ~2.0 (head_top 1.751 + clearance) | derived            |
| **Interior ceiling**  | ≥ ~2.2 min, ~2.5–3 comfortable | derived                  |
| Bed length / top      | ≥ character_height (~2) / ~0.5–0.667 | derived           |
| Item grip / size      | subcube/microcube, relative to hand reach | derived       |

These feed **both** the static validator (reject a 1.5-cube ceiling, a 0.3-cube door, a
character-height seat) **and** the visual gate: critique already renders the **reference character
beside the asset** (`show_reference_character`, default on). Make "reads at correct scale next to the
humanoid" an explicit scored criterion, not just an aesthetic vibe.

## Subcube / microcube detail as a styleable layer

Detail is a **separate greeble pass** keyed off the spec + style, run after the structural pass —
never baked into geometry. Examples: mullioned windows (microcube muntins), carved door lintels,
corner quoins, exposed beam framing, tapered columns, railings. Generalize the existing
`door_frame` / `window_frame` / `railing` / `half_wall` / `pitched_roof` into a data-driven
"detail ruleset per style." (Honors the standing rule: never default to full-cube Minecraft models.)

## Closed-loop confirmation (three gates)

1. **Spec validation** (static, no engine): connectivity graph + clearances + **scale checks against
   the character canon** (ceiling/door/seat/table within bounds). Fast fail.
2. **Functional validation** (in-engine): build it, rebuild navgrid, pathfind spawn → each room
   marker and across every floor; verify doors open/close and **block when locked**; for furniture,
   verify interaction points are usable (sit/stand resolves). Reuses `StructureResult.locations`
   markers + IE validation tools.
3. **Visual + scale critique** (vision model): generalize `critique_template` to a *region/structure*
   (orbit screenshots **with the reference character beside it**), scored on aesthetics, "does it read
   as a {function}", **and "is it correctly scaled next to the humanoid"** → feeds revisions back into
   the spec. MineBench's judged loop, automated, function- and scale-aware.

## Phased roadmap

- **P0 — The contract.** Define + unit-test the `ScaledSpec` base + `BuildingSpec`, the static
  validator, **and the character scale-canon loader/checks**. Nothing renders yet; de-risks
  everything downstream.
- **P1 — Deterministic realizer from spec** (single-story, house), reusing `StructureGenerator`
  primitives, **with real placed + registered functional doors** and character-correct ceiling/door
  heights. Visual + functional + scale verify (reference character in shots).
- **P2 — LLM spec author** (prompt → spec, schema-constrained, scale-canon in the prompt, repair
  loop). LLM + deterministic genuinely combined.
- **P3 — Subcube/microcube detail pass** as a styleable layer.
- **P4 — Furniture tier**: parametric furniture specs sized to the canon (seat/table/bed/counter),
  generatable standalone **and** as building `fixtures`; wire interaction points via
  `InteractionProfileManager` / IE validation. Then multi-story + stair connectivity + room programs
  for more functions (shop, church).
- **P5 — Item tier** (parallel small-asset track): item specs with grip point + equip slot, sized
  to hand/character, anchorable onto furniture surfaces. Reuses the existing
  `generate_asset`/`critique` machinery with the scale gate switched on.
- **P6 — Functional + scale validation harness** (navgrid path tests, IE sit/stand checks, scale
  asserts) + vision critique → auto-revise spec. Full closed loop.
- **P7 — Special/large** (stadium) + persist whole buildings/furniture/items as composite templates
  spawnable anywhere.

## P0/P1 concrete starting point

**P0 (contract):**
- `BuildingSpec` schema (JSON Schema or a typed dataclass in `tools/`).
- Static validator: room-overlap, portal-connectivity graph (entrance → every room), stair
  story-linkage, opening/ceiling clearance vs character bounds. Unit tests with good + bad specs.
- A couple of hand-authored example specs (a house, a deliberately-broken house) as fixtures.

**P1 (realizer, house):**
- Add a spec-driven entry to `StructureGenerator` (e.g. `generateFromSpec`) that composes existing
  primitives per room/portal/story.
- For each `door` portal: cut the opening, place a door-leaf template, `registerDoor` with
  hinge/rotation/lockable/key.
- Emit `StructureResult.locations` markers per room for the P5 path-test harness.
- Verify per the MANDATORY loop in CLAUDE.md: build → launch → build a house → orbit screenshots +
  confirm a registered door that opens, and locks when keyed.

## Key code touchpoints

- `engine/src/core/StructureGenerator.cpp` / `.h` — realizer (extend here).
- `engine/include/core/DoorManager.h` — `registerDoor` / `setLocked` / persistence.
- `engine/src/core/EngineAPIServer.cpp` — `build_structure` / `list_structure_types` handlers.
- `scripts/mcp/phyxel_mcp_server.py` — `build_structure`, `register_door`, `set_door_lock`,
  `critique_template` / `refine_template` (generalize for structures), `orbit_screenshots`.
- `tools/building_prompts.py`, `tools/blocksmith_generate.py` — existing LLM building path (becomes
  a spec author / reference).
- `resources/biomes.json` — pattern to copy for a data-driven `room_program` library.
- `resources/character_design_constraints.json` — **the scale canon** (character 1.751 cubes;
  seat/table/backrest targets). Re-measure after `.anim` changes.
- `engine/src/core/InteractionProfileManager.cpp`, `SeatInteractionHandler` + IE tools
  (`get_character_design_constraints`, `validate_ie_pose`) — furniture interaction-point support.
