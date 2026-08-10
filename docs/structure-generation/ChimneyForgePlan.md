# Chimney Forge — plan the stack, don't stamp it

Status: **planned, not built** (2026-08-09). Answers the standing design-key gate
(`docs/FeatureDesignKeys.md`) before any code.

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
