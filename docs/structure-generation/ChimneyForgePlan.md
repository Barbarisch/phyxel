# Chimney Forge — plan the stack, don't stamp it

Status: **BUILT 2026-08-10** (`HearthForge`, `engine/{include,src}/core/HearthForge.*`).
Planned 2026-08-09 against the standing design-key gate (`docs/FeatureDesignKeys.md`);
the plan below is kept as written, with a "what shipped" section at the end recording
where the build followed it and where it went further.

## The problem, measured

The chimney is currently the only structural element built *after* the shell. It is
stamped into a finished building as loose micro voxels by the furnish stage, and it
fights the building it lands in:

| symptom | cause | status |
|---|---|---|
| stack full of gaps | micro writes into cells owned by a full cube were REFUSED | **fixed** (900b0dc4 — refine instead of refuse) |
| flue solid at every storey | inner 3x3 was un-emitted, not cleared; floor/roof stayed inside it | **fixed** (900b0dc4 — `StructureResult::clears`) |
| runs up through the middle of a floor | sited on the hearth, blind to the storey above | **OPEN — this plan** |

The destructive-write ledger (`PlacementResult::displaced`) reports **~600 displaced
cells per tavern build**. That number is the whole problem in one figure: a pass that
is part of the building should displace nothing.

**Success criterion: displaced == 0 for the chimney pass.** Measurable, not a
screenshot judgement.

## Why it can't just be "route around the floor"

The stack needs hearth positions. Hearths are placed by `FurniturePlacer` during
**furnish**, which runs *after* **realize**. So the chimney cannot be planned into
the canvas without hearth positions being known earlier. This is the reordering the
user identified independently: *the floorplan should settle all floors and their
fixed points before any furniture pass runs.*

## The change

1. **Hearths become PROGRAM fixtures, not furniture.** A hearth is a built-in that
   carries a flue through the roof — structurally it is closer to a wall than to a
   stool. `autofillRoomLayout` sites it (room purpose + wall preference); the
   `BuildingProgram` carries it into realize.
2. **`realizeShell` paints hearth + stack into the MicroCanvas**, with the flue as
   canvas air, so both export with the shell through the same greedy coarsening.
3. **The furnish stage stops building chimneys.** `FurniturePlacer` treats the
   already-built hearth as an occupied reservation (it must not place a stool in the
   firebox), and `place_chimney` disappears as a post-hoc pass.
4. **Routing:** the stack rises from the mantel and must clear every floor span it
   crosses. Where a storey's floor would intersect it, the slab yields (a flue
   opening, recorded like a stair well) — the stack does not detour. A stack that
   cannot reach the apex without crossing a room's walkable centre is a REFUSAL, not
   a silent lean.

## Design keys (the standing gate)

- **Procedural pipeline?** Yes — moves INTO the pipeline, from a post-place patch to
  a realize-stage element. Disturbs `furnish` (loses a pass) and `floorplan` (gains
  hearth siting); nothing else reads chimney state.
- **API?** No new surface. `build_structure` unchanged. `chimneys_built` stays in the
  response; `displaced_existing_voxels` should stop appearing for chimneys.
- **Visual test?** Small flat world, one tavern, elevation + interior at the hearth,
  plus a section through the stack.
- **Small test world?** Yes — the existing `TavernTest` flat single chunk.

## Validation (depth planned up front)

- **L2** `WallClosure` + `FloorIntegrity` now cover the chimney for the first time —
  they run on the realized canvas, which is where the stack will live.
- **L2 new** `ChimneyIntegrityTest`: the flue is continuous air from firebox to
  cap; the stack is solid ring at every course; the stack never intersects a room's
  walkable floor span.
- **L4** ledger `displaced == 0`, and the visual.

## Traps (paid for already)

- ⚑ `crawlHeightCubes` measures an occupiable void — it is 0 for a crawlspace. Do not
  key substructure geometry off it (cost one silent no-op).
- ⚑ Erase at the resolution you placed at. Clearing whole cubes over a bbox is what
  put bays in the walls.
- ⚑ Canvas detectors cannot see anything that happens after export. Once the stack is
  in the canvas they can — that is half the point of this change.

## What shipped (2026-08-10)

`HearthForge` — a forge in the `docs/ForgePattern.md` sense (grounded presets →
plan → rasterize into the shared `MicroCanvas` → the shell's own greedy emit).

| plan item | shipped as |
|---|---|
| hearths become PROGRAM fixtures | `StructureForge::siteHearths` runs in **floorplan**, writing `ProgFixture`s (with a real `rotation`) onto each story; re-run after the program gate's repair re-roll |
| `realizeShell` paints hearth + stack | `HearthForge::paintBody` per story (after that story's walls), `paintStack` after the roof — the flue is carved as canvas AIR, so floors + roof deck yield by construction |
| furnish stops building chimneys | the `place_chimney` post-pass, `planChimneyStack`, and the flueless-hearth removal are **deleted**; furnish still *places* the hearth so its cells stay reserved, but spawns no template over the shell's masonry |
| routing / refusal | a stack that would rise through the middle of an upstairs room **refuses the shell** (`ChimneyIntegrityTest.RefusesAStackThroughTheMiddleOfAnUpstairsRoom`) |

Beyond the plan, because the move made them possible or necessary:

- **The flue now reaches the fire.** Every preset carves a THROAT from the firebox
  through the mantel into the flue. The old stack rested on a solid template mantel:
  there was no air path from fire to cap at all — a chimney-shaped decoration. This
  is what the new L2 flood-fill measures.
- **The hearth is generated, not spawned.** `fireplace` / `forge_hearth` /
  `oven_bread` bodies are painted from presets carrying the SAME
  `object_dimensions.json` numbers the templates were generated against
  (`HearthForgeTest.BodyDimensionsMatchTheGroundedCanon`). The library templates
  still exist and still spawn by hand; structure-gen no longer uses them.
- **Stack columns are reserved upstairs**, on both sides of the pipeline
  (`FurniturePlacer::planReservedRects` at furnish time, an accumulating list at
  siting time) — otherwise a first-floor bed lands inside the chimney breast.
- **`ShellResult::roofApexMicro`** records the ridge measured *before* the stacks
  were painted; the signage pass reads it instead of re-deriving an apex from the
  finished canvas, which would return the chimney cap.
- The hearth **fire light** is registered from the record's measured flame anchor
  (`AssemblyPlan::HearthRecord`), not from a placed object's base + an offset.
- `featureAt` answers **"hearth"** for body and stack cubes.

Response fields: `chimneys_built` is now the count of stacks the SHELL built;
`hearths_builtin` reports how many placements were satisfied by it;
`flueless_hearths_removed` is gone (an unbuildable stack refuses instead).

## Follow-up SHIPPED 2026-08-10: the fire is items, not masonry

User: *"the burning logs should be separate… depending on the size should have
different number of logs and actual fire effects like we have with torches."*
Right on all three counts, and the item pipeline turned out to be the only design
that satisfies the third one.

- **The fuel left the brickwork.** `HearthForge` no longer paints a `Log`/`glow`
  bed into the firebox (that bed was what read as a pale slab). It PLANS the fire
  instead — kind, count and poses recorded on `HearthRecord` — because realize is
  the only stage that knows the firebox's clear span.
- **Count scales with the firebox**: a base row of billets on the `kBilletPitchMicro`
  pitch across the clear DEPTH, plus a crossing log once there's a bed to rest on;
  capped at `kMaxFuelBillets`. The standard 0.44 m-deep firebox lays 3.
- **Two new fine-voxel items** (`tools/gen_items.py` `_billet`): `firewood` (inert)
  and `flaming_log`, grounded to stove-length split firewood (0.35 × 0.11 m).
- **The flame is a declarative ITEM EFFECT** — `items.json` `effects: [{vfx, light}]`,
  the same mechanism the torch uses. Exactly ONE billet is lit, because every
  effect light is a real point light and `MAX_POINT_LIGHTS` is 32 engine-wide.
  The hearth's old static `addPointLight` is gone; the fire carries its own.
- **Why item-driven beat a hearth-owned VFX field** (my initial recommendation,
  overruled): item props persist, and `ItemPropManager::rebuildFromPlacedObjects`
  re-registers their effects on load — so a lit hearth relights itself. A VFX
  field spawned at build time would not; VFX and lights are runtime-only.

### Two engine defects this surfaced (both fixed here, both wider than hearths)

1. **Every item prop was takeable.** `registerItemProp` added a pickup point
   unconditionally, so the nailed-up Prancing Pony sign and the chamber rugs could
   be pocketed. `ItemDefinition::fixed` now suppresses the pickup point (and is
   recorded in metadata so it survives a reload). Set on the fuel, the sign, the rugs.
2. **Item props did not reload IN PLACE.** The reload path rebuilt the transform by
   hand from the placed object's INTEGER cell and never restored `localCOM` — so a
   restored prop moved (a hearth log dropped 0.67 u) and its effects anchored off
   its own model, which would have hit torches and lamps too. Both paths now go
   through one `ItemPropManager::buildPropGeometry`, and the exact resting pose is
   persisted as placed-object metadata. Verified live: fire light y = 17.7447
   before save, 17.7447 after a cold restart.

**Scope:** fireplaces only. The forge (charcoal) and bake oven (fired then swept)
keep their painted ember beds — they burn something that is not cordwood and each
needs its own grounded fuel, pinned by `HearthFuelTest.NonCordwoodHearthsLayNoLogs`.

## Then: hearth typology (separate change, archetype sheet first)

One red-brick stack is wrong for a medieval tavern — brick was expensive and
regionally rare in England before the Tudor period; the vernacular is rubble or
dressed **stone**. A taproom wants a wide hooded **inglenook**: 2-3 m across, deep
enough to sit in, a timber bressummer spanning the opening, the flue gathering above
it into a broad stone stack. Make hearth+stack a style/typology-driven family —
inglenook+stone for the tavern, plain stone for cottages, brick for later-period or
high-status, smithy keeps its industrial flue. Materials already live on the style
profile, which is where this axis belongs. **Needs a grounded archetype sheet before
any code.**
