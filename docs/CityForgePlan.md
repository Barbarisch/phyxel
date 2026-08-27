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

### M3 — Density & palette (quick wins, if time)
- Core ring: no picket fences (city cores aren't fenced crofts) — fence gate keyed off ring
  membership; data flag `core_fences: false` (city only).
- Palette: dedupe glut (7 taverns) — cap per-typology share in the draw for business types;
  or weight tune in data. Smallest honest change: weights tune + a `max_count` per typology.

### M4 — `tenement` typology (apartments) — NEXT session (needs archetype sheet)
2–3 story stacked one-room dwellings, shared stair, gable-to-street; core-ring weighted.
Generative multi-story mechanism exists (inn chambers). Requires grounded room program +
furnishing recipes + conformant assets first (REFUSE-ON-ANY-GAP applies).

### M5 — `town_hall` typology (civic) — NEXT session (needs archetype sheet)
Guildhall/moot-hall fronting the square on a RESERVED civic plot (layout change: civic plot
reservation). Constraint: 7-cube cruck span caps hall width — aisled frame style is the known
open gap (StructureForge queue #5).

### Logged follow-ups (docs/StructurePipelineGaps.md)
- Residents job counter reports 0 while residents spawn (baseline evidence above).
- Secondary streets host no building frontages — block interiors stay empty grass; secondary
  infill rows are the real density lever after M4.
- MCP `get_job_status` claims "No game project is loaded" while the engine has one (HTTP
  `/api/jobs` fine).
