# Structure Pipeline — Engine / Capability Gaps

Running log of things we *want* while building structures that the engine or the
realizer doesn't support yet. Add an entry the moment we hit a wall; clear it when
implemented. Newest first.

Format: **[area] short title** — what's missing · why it matters · rough fix.

---

## Open

### [engine] NavGrid not rebuilt after runtime world edits / structure spawns
The Tier-C runtime playtest asks `/api/navgrid/path` to confirm a character can navigate to
each room. But the NavGrid is built once (at world load, over terrain) and is NOT refreshed
after `world/clear`, `world/fill`, or a structure spawn. After building a house at runtime,
`navgrid/cell` reports a stale `surfaceY` (e.g. 28 over a y=15 pad) and `navgrid/path` returns
`found:false` even for a clearly walkable building. Doors work fine; only navigation is affected.
- *Why it matters:* Tier C can't verify navigation of freshly-built structures; runtime nav for
  any procedurally placed building/edit is unreliable.
- *Fix:* a `rebuild_navgrid` API (like `rebuild_physics`) — or auto-rebuild the affected NavGrid
  region after world edits / `placed_object` spawns. Until then Tier B (offline walkable grid) is
  the authoritative navigability check and Tier C navigation is advisory-only.

### [realize] Roof doesn't cover lower (single-story) wings
The realizer only roofs the **top story's** footprint. In a multi-story building whose
ground floor is bigger than the upper floor (e.g. the Burgomaster's Mansion: 2-story
front block + single-story wing tails), the parts of a lower story that stick out past
the top story are left **open-topped**. Visible in the mansion: the rear ends of both
wings have no roof and you can see the interior floor from above.
- *Why it matters:* any articulated multi-story massing (wings, ells, lean-tos) looks
  unfinished and isn't weatherproof / enclosed for gameplay.
- *Fix:* roof **every exterior-exposed top surface at its own local height** — walk all
  occupied (x,z) across all stories and cap each column at the cube above its tallest
  room, not just the global top-story union.

### [realize] No pitched / hipped roof over non-rectangular (L / U / T) outlines
Pitched roofs only trigger when a footprint is a **true rectangle**; L/U/T get a flat
roof with coping instead. The mansion only got a pitch because its *upper* story happened
to tile a clean rectangle.
- *Why it matters:* real manors/cottages want a pitched roof that follows the articulated
  plan — hips at outer corners, valleys at reentrant corners.
- *Fix:* a hip-roof generator over an arbitrary outline (medial-axis / straight-skeleton
  ridge, sloped slabs to each eave, valleys where wings meet). Bigger task.

### [content/pipeline] No "boarded / shuttered" window infill
Window openings are framed reveals left **open**. Curse of Strahd specifically calls for
**boarded-up** windows; taverns want shutters; nice homes want glass.
- *Why it matters:* mood + correctness for specific scenes; an open hole reads as ruined.
- *Fix:* a window-infill style on the portal (`open | glass | boarded | shuttered`) and
  realizer support to fill the reveal with Glass / crossed Wood planks / subcube shutters.

### [content] No courtyard / yard props (fountain, well, garden)
The mansion's rear courtyard is empty grass. The module describes a **dried fountain**.
We have no fountain/well/planter templates and no "place a prop in this open cell" path.
- *Why it matters:* courtyards and yards are dead space without features.
- *Fix:* a small library of yard-prop templates + an optional `props` list on open regions.

---

---

## Asset generation backlog (things to generate / acquire so structures can use them)

> Update 2026-06-20: chair/table/bed regenerated as detailed multi-res via
> `tools/structure_pipeline/furniture.py` (chair 16->138, table 17->254, bed 47->416 voxels,
> clean Wood, correct scale, sit-point preserved). Still TODO below: stool/bench/bookshelf/etc.

### [engine] Furniture detail capped by microcube resolution (0.11 m)
The smallest voxel is a microcube = 1/9 cube = 0.11 m. A 0.5 m chair is only ~5 micro-cells
across, so small furniture is inherently blocky no matter how carefully authored — curves, thin
turned legs, and fine ornament aren't expressible. To get genuinely smooth furniture the engine
would need a finer voxel tier (or per-template mesh assets). Note, not necessarily fix.

### [materials] No soft/cloth materials for bedding & upholstery
Beds use Sandstone (mattress) + Sand (pillow) as stand-ins; there's no linen/cloth/wool/cushion
material. Upholstered chairs, drapes, rugs all want this. Belongs with the materials/textures item.


From manual inspection of the Burgomaster's Mansion (2026-06-20). The user wants this list kept
current — when a structure needs an asset we don't have, add it here.

### Furniture — regenerate as detailed multi-resolution (CURRENT ONES LOOK BAD)
`chair_wood` (16 subcubes), `bed_single` (47), `table_wood` (17) are crude subcube-only blocks
with ZERO microcube detail — they violate the "exploit subcubes/microcubes, never full-cube" rule
and read as ugly/overlapping. Regenerate via the DetailCanvas (turned legs, beveled edges, seat
back slats, mattress + pillow + blanket relief, table aprons) or BlockSmith. Needed set: chair,
stool, bench, table, desk, bed, wardrobe, dresser, bookshelf, chest, cabinet, barrel, rug.

### Door variety — LIBRARY + DETERMINISTIC SELECTION DONE (2026-06-21)
`tools/structure_pipeline/doors.py`: a door catalog (door_plank/door_wood/door_iron/door_wood_wide/
door_grand/gate_timber) with a deterministic `select_door(width, purpose, lockable, exterior)` that
the realizer AND the checks share. Variable openings: the selected door drives the carved opening
size (grand entrance = 2x3, bedroom = 1x2 plank, cellar = iron, courtyard = 3x3 gate, lockable
bedroom = panel). Checks: `opening_fit_report` (opening == selected door, in carved air),
`door_selection_report` (fits the wall; lockable portal gets a lockable door). Tests: test_doors.py.

Remaining door work (OPEN):
- **[engine] Free-swinging (physics) doors.** Catalog marks door_grand/gate_timber `swing="free"`,
  but DoorManager only does kinematic open/close. Need physics free-swing (push, momentum, settle).
- **Door handedness / MIRRORING.** All leaves hinge at local X=0 (left-hung). Need: the realizer to
  choose the hinge side so the leaf swings into the clear side (deterministic, vs the current swing
  check that just asks "is SOME side clear"), and mirrored (right-hung) templates (or an engine flip
  flag — engine has rotation but not mirror). Plus a `door_handedness_report` deterministic check.
- More sizes (very wide gates >3, arched-top doors, portcullis) + per-door art polish.

### Lighting fixtures + emission (none exist as props)
Need lamp props AND light emission: wall sconce, candelabra, chandelier, floor lantern, hearth/
fireplace glow. Engine HAS point lights (`/api/add_point_light`), but nothing auto-places a lamp
prop + a co-located point light. Add lamp templates (glow material + frame) and have the realizer
drop sconces along halls / a chandelier in big rooms + register point lights at them.

### Materials / textures (user wants more + better)
Candidate additions for interiors/manors: dark/stained wood, plaster/stucco wall, wallpaper,
marble/tile floor, carpet/rug, slate or wood-shingle roof, drapery/curtain. Current 19 materials
skew exterior/terrain. (Adding a material = source PNG in resources/textures/source/ + materials.json
+ rebuild atlas via build_shaders.bat.)

### [realize] Mansion/grand-building SCALE too small
Rooms read cramped: 6-wide rooms + 3-cube ceilings for a "grand" manor. Grand buildings should use
taller ceilings (4-5 cubes) and larger rooms. Fix in the author prompt (per-function scale guidance)
and/or canon (a "grand" ceiling target). Not an engine limit — a tuning/prompt gap.

## Resolved (verified by deterministic checks in tools/structure_pipeline/geometry.py, not by eye)
- **Stair-top blocked** — root cause: the realizer carved the stairwell hole during the lower
  story, then the upper story's floor slab refilled it. Fixed by deferring all stairwell-hole
  carving until after every floor is placed. Caught + confirmed by `stair_clearance_report`
  (per-step headroom).
- **Floating furniture (table top detached)** — caught by `connectivity_report`; table legs now
  reach the underside of the top. Furniture generator self-checks connectivity on write.
- **Door openings too wide for the leaf** — the realizer placed one leaf in any opening; now it
  TILES leaves (`door_leaves_for_width`) so a width-4 grand door is two 2-wide leaves, no gap.
  Confirmed by `opening_fit_report`.
- **Windows at floor level** — realizer now sets a 1-cube sill.
- **Roof didn't cover single-story wings** — realizer now caps EVERY column at the top of the
  highest story occupying it (stepped roof: pitched/flat over the top level, flat-coped over lower
  wings). Caught + confirmed by `roof_coverage_report`.
- **Fixtures clipping walls / overlapping (real sizes)** — `fixture_placement_report` measures the
  actual rotated template footprint (not the spec's f.rect) and checks room-fit + wall-clip +
  fixture-overlap. Caught a real 4x2 counter poking through a wall in examples/house_L.

New deterministic checks now in the build gate (geometry.py, tested in test_geometry.py):
stair per-step headroom, door opening↔leaf fit, floating-component connectivity, real-world
dimensions (BED_SIZES/REFERENCE_DIMS), fixture placement (real footprints), roof coverage,
per-cell room headroom.

> Materials/textures needs moved to a dedicated, actively-maintained doc: **docs/MaterialTextureNeeds.md**.
