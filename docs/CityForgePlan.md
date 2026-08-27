# CityForge — pushing `tier:"city"` from "big village" to "city" (2026-08-26)

**Goal.** `POST /api/settlement/build {tier:"city"}` should produce something that *reads* as a
fantasy city: a dressed market centre (stalls + statue), meandering streets, vertical/civic
massing (tenement + town hall typologies), dense core. The engine generates everything
(provenance rule: no hand-placement presented as generator output).

**Baseline measured 2026-08-26** (SettlementTest, Flat 6×6 chunks at x/z 320–511, seed 7,
160×160, Release): 33/33 buildings built, 5 streets, 259,820 paved columns, 51 yard props,
28 residents live (job counter said `spawned: 0` — counter regression, entities exist; logged).
Honest read: spread-out village. Square = wider pavement + well. All 1–2 story cottages behind
picket fences. Straight streets. 7 taverns / 5 blacksmiths from thin palette weights.

## Design keys (docs/FeatureDesignKeys.md — answered up front)

- **Pipeline stage:** all work lands in the settlement generation stage (SettlementLayout →
  StreetPaver → SettlementBuildService units), downstream of terrain/flora, upstream of nothing.
  Layout stays a pure function of (tier preset, W, D, seed) — chunk-independent by construction,
  so no chunk-visibility risk. No render-side changes.
- **API:** no new endpoints; `settlement_program.json` gains data keys (echoed via the existing
  program echo in the build response). Unknown keys absent = legacy behavior byte-identical.
- **Aesthetic:** new assets (market_stall, statue) are micro/subcube-resolution architecture
  templates via the deterministic `regen_furniture.py` pipeline — no full-cube prop bodies.
- **Visual test plan:** small world = the existing SettlementTest flat region; L4 = rebuild the
  same seed-7 city and orbit-screenshot the square + streets; deterministic checks are the
  primary gate (L2 invariant tests + L3 walkability probes), screenshots corroborate only.

## Milestones

### M1 — Market centre dressing (stalls + statue) ✦ SHIPPED 2026-08-26
- **Assets** (`tools/regen_furniture.py` → `resources/templates/architecture/`):
  - `market_stall` — timber trestle stall: plank counter ~1.8×0.9 m at ~0.9 m, posts to ~2.2 m,
    pitched cloth canopy (Linen/Wool stripes). Dims REASONED from trestle-table + market-cross
    stall norms (6 ft stallboard); NEEDS-RESEARCH tag carried in the header like other
    settlement `sources`.
  - `statue_hero` — stone figure (~2.2 m, heroic scale) on a stepped plinth (~1.3 m);
    Stone/StoneBricks. Fantasy-setting deliberate choice; the period-strict alternative (market
    cross) noted for a later era pack.
- **Placement:** new pure planner `planSquareDressing(square, throughStreets, wellRect, seed,
  spec)` in SettlementLayout → consumed by a new SettlementBuildService unit after
  "yard props + well". Statue at square centre; well relocated to a corner pad; stalls fill the
  four corner pads (square minus the two through-street bands minus 1-cube clearance), fronts
  facing the nearest street. Data: `public.market_square` gains `{"stalls": N, "statue": true}`
  (city: statue + stalls; town: stalls only, well stays centre). Absent keys = today's output.
- **Validation:** L2 red-first `MarketDressingTest` — inside-square, no street-band overlap, no
  pairwise overlap + ≥1 cube clearance, statue centred, well not displaced onto a stall,
  deterministic in seed. L3: flood-walk from each through-street into every corner pad and to
  each stall front (dressing must not wall the square). L4: seed-7 rebuild + orbit shots.

**M1 status:** assets shipped (stall canopy A/B'd; statue figure StoneTiles after a Stone/Sandstone
A/B — dark Stone reads as a totem); `planSquareDressing` + `MarketDressingTest` (4 tests, red-first)
green; service wired. **⚑ ORDERING FOOTGUN (found live):** the square is inside the swept road
band — the late "street sweep" unit clears whole cells over it, so dressing placed in the
yard-props unit registered 5 props while the world had ZERO standing voxels (registry said yes,
scan_region said Dirt — verify the WORLD, not the registry). Dressing now runs in its own unit
AFTER "street sweep", before "nav rebuild".

### M2 — Meandering streets ✦ SHIPPED 2026-08-26
Secondary lanes (which host no frontages — safe to bend) are now CHAINS of straight runs with
seeded lateral jogs of 1..laneWidth−1 cubes; consecutive runs keep an edge overlap ≥ 1 cube so
every lane stays one connected walkable network, and jogs never enter the crossroads exclusion
zone or the end margins. `vertBands` carries each lane's u-band union so plot rows and cross-row
depth stay honest. **Validated:** `CityLayoutTest.SecondaryLanesMeander` red-first (0 jogs/16
lanes → green across 4 seeds), jitter-band test adapted to merged lane bands (gap floor relaxed
by 2·(laneWidth−1) drift), full 87-test settlement sweep green, L4 seed-7 city = 17 street rects
with visible doglegs (docs/evidence/cityforge_m2_meander_top.png). Main + cross axes stay
straight on purpose (they host the burgage frontages).

### M3 — Fences that behave — SHIPPED 2026-08-27 (user-approved rules)
`shouldFencePlot` (pure, `FencePolicyTest` red-first): (1) core-ring plots NEVER fenced;
(2) a building within <1 cube of its plot boundary goes unfenced (flush setback-0 rows aren't
caged — the user's "more space between fence and structure"); (3) seeded per-plot fraction,
tier data `fences.fraction` (village .85 / town .7 / city .5; absent = 1.0 legacy). Gate now
TRACKS the front door: `fenceGateWindowAt` centres the cube-aligned gate on the paver's spur
anchor (footprint front-wall midpoint), clamped inside the run. Result surfaces
`unfenced_by_policy`.

### M3b — Density knob — SHIPPED 2026-08-27 ("a very dense city")
`density` request param on `/api/settlement/build` (0.5–2, clamped, echoed in
`program.density`): `applyDensity` (pure, red-first test) scales blocks/plot-depth/side-gap/
setback DOWN and buildings UP (bounded: blocks ≥8, depth ≥6), and thins fenceFraction.
1.0 = identity, legacy byte-compatible.

### M3c — Business SIGN ITEMS — SHIPPED 2026-08-27
ROOT CAUSE of "no signs": the settlement path never passed ItemPropManager
(`SettlementBuildService` had no `itemProps` dep), so EVERY sign item — the tavern's Pony
included — silently fell back to the blank `hanging_sign` board (26 blank boards counted in
the placed-object dump), and settlement interiors got no tableware items either. Deps wired
end-to-end (settlement + worldforge callers). Plus five authored default trade boards
(`tools/gen_trade_signs.py` art — symbol-first per medieval practice: anvil/pretzel/balance/
mortar/cleaver + one caption word — → `gen_items.py` flat boards → materials.json + items.json
+ room_program.json `sign_item`). All 5 asset-request rows flipped conformant
(`asset_requests.py --check` clean).

### M3d — SIGN MOUNT v2 — SHIPPED 2026-08-27 (user feedback round 2)
- **Orientation fixed at the ROOT**: a z-projected board's BACK face rendered its art rotated
  180° (the "upside-down smithy"). Settled by crop-verified A/B across four candidate UV
  mappings (analytical derivations kept losing to per-face quad conventions + greedy-merge):
  the -Z face samples the OPPOSITE image slice reversed (`offset 1-fn, scale -sn`, both axes)
  — boards are now genuinely two-sided (KinematicVoxelManager buildFaces case 1).
- **Projecting signs HANG from a real bracket**: `SignMount.bracketCells` — wrought-iron arm
  anchored in the wall, scroll tip, diagonal brace, hanger links — stamped as static Metal
  micro voxels by the furnish stage (L4: 25 micros, displaced 0, visible over the tavern door).
- **World-probe clearance**: planSignMount takes a `solidAt` micro-probe (cube/subcube/micro,
  the place()-ledger occupancy rule); a projecting pose that would intersect an eave/jetty
  repairs to FLUSH; flush blocked above the door repairs to BESIDE the door (right, then left,
  at door height — probe-gated so a blind beside-pose can never cover a window); then skip
  with the reason. Recovers the 8 skipped-for-eave signs. `SignMount` suite 13 tests
  (4 new, red-first). Response gains `beside_door`/`bracket_cells`; `over_door` is now honest.
- Evidence: docs/evidence/cityforge_m3d_sign_bracket_{north,south}.png (both faces readable).
- Punted (logged): loosely-hanging signs that swing on collision — needs a hinge constraint
  on fixed item props (KinematicAnimator has hinges but no physics coupling).

### M4 — `tenement` typology (apartments) — TODO (user-confirmed want)
2–3 story stacked one-room dwellings, shared stair, gable-to-street; core-ring weighted.
Generative multi-story mechanism exists (inn chambers). Requires grounded room program +
furnishing recipes + conformant assets first (REFUSE-ON-ANY-GAP applies).

### M5 — `town_hall` typology (civic) — TODO (user-confirmed want)
Guildhall/moot-hall fronting the square on a RESERVED civic plot (layout change: civic plot
reservation). Constraint: 7-cube cruck span caps hall width — aisled frame style is the known
open gap (StructureForge queue #5).

### M6 — `mansion` typology — TODO (user-asked 2026-08-27)
Urban magnate house beyond manor_hall: courtyard/L-plan, high wealth tier. Needs archetype
sheet; L-plan multi-story (KI-5g) is a known owed mechanism.

### M7 — Castle / keep + town WALLS — TODO (user-asked 2026-08-27)
The city edge earns a circuit wall + gatehouses (place_town_wall #42 is L0 in the ledger);
castle/keep as a reserved precinct anchored off the axes. Big: needs its own plan (wall
grounding, gate alignment with arriving WorldForge roads, keep typology).

### Polish backlog (user feedback 2026-08-27, logged in StructurePipelineGaps)
- Floating foliage: leftover canopy pieces hang in the air after site clearing/felling.
- Terrain: settlement placement should tolerate gentle elevation — flatten locally where a
  building needs it, keep hills elsewhere (today's look is too flat/terraced).
- Interior point lights BLEED through walls — exterior walls glow at night (engine lighting:
  no occlusion on placed lights; blocklight phase 2 is the real fix).

### Logged follow-ups (docs/StructurePipelineGaps.md)
- Residents job counter reports 0 while residents spawn (baseline evidence above).
- Secondary streets host no building frontages — block interiors stay empty grass; secondary
  infill rows are the real density lever after M4.
- MCP `get_job_status` claims "No game project is loaded" while the engine has one (HTTP
  `/api/jobs` fine).
- Typology glut: 7 taverns / 33 buildings — per-typology max-share cap in the draw.
