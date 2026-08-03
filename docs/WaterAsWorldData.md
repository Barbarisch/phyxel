# Water as World Data — making water physically real

> Status: **DESIGN, nothing built** (2026-08-03).
>
> **USER DIRECTIVE (verbatim):** *"water must exist on top of terrain. it should never be possible to
> create a body of water that isnt tied to the physical boundaries of a terrain."* and *"when water
> is generated, it should 'exist' in that space, and the rules for whether you can create water to
> inhabit that space needs to be tied to real physical rules. That means water can rest on itself
> and static voxel terrain."*
>
> This supersedes the render-side patches contemplated in
> [`docs/WaterAppearanceV4.md`](WaterAppearanceV4.md) §0. Appearance work (v4) is orthogonal and
> should be parked until water is in the right place — there is no point tuning how water looks
> while it is floating.

---

## 1. The defect, measured

At `WaterTableTest` (4270,152,−14848) a lake sheet lies over a grass hillside with a sheer vertical
wall of water at its edge. The engine's own validator over that view:

```
columns 96838   wet 62302   rim 606   rim_leaks 606   worst_leak_depth 38.0
```

**A 100% rim-failure rate**, terrain up to **38 voxels** below the water level beside it. The repo's
earlier record shows the same pattern elsewhere (65/65 on a coast) and an open integrity note from
2026-07-10: *"the 'floating slab' at patch edges is NOT fixed"*. This is systemic and long-standing.

## 2. Why it happens — two height sources that never meet

| | resolution | role |
|---|---|---|
| **Hydrology bake** (`HydrologyMap`) | **128 m per cell**, coarse height model | Priority-Flood finds basins, fills each to its spill level |
| **Voxel terrain** (`WorldGenerator`) | **per voxel**, with ridged-multifractal detail the coarse model lacks | what you actually stand on |

A "basin" in the coarse model can be a **ridge** in the world. `WaterRenderPipeline` draws a
camera-following clipmap sheet displaced to the baked level; nothing reconciles it with the ground.
The only runtime tie is a **depth-buffer** dry-land gate, which cannot fix water the bake genuinely
places above lower terrain and **cannot fire at all where terrain is not drawn**.

### ⚑The truth already exists at generation time and is thrown away

`WorldGenerator.cpp:938-941`, in the flora gate:

```cpp
const float wl = m_hydro->waterLevelAt(jx, jz);
if (wl > NO_WATER*0.5f && (float)col.surfaceY < wl) return false;  // under lake/sea
```

That is **exactly** the physical containment test — real per-column terrain surface vs. water level,
at full terrain resolution. Generation computes it, uses it to keep trees from growing underwater,
and then discards it, deferring to "the water runtime" (the coarse overlay). **The engine already
knows, per column, where water belongs. It just never writes it down.**

## 2b. ⚑THE GOVERNING RULE — terrain is the truth, water is the consequence

**USER (2026-08-03):** *"Water bodies should be defined by the terrain holding them, not the other
way around."*

This inverts the current architecture and is the principle everything below must obey. Today the
bake decides a water level and the world is dressed around it; the level is an *input*. Under this
rule the level is an **output**: terrain exists, and water is whatever that terrain holds.

Consequences that fall straight out of it:
- **The hydrology bake stops being a source of truth and becomes a HINT.** It may propose candidate
  basins cheaply over 32 km; it may not decide where the waterline is.
- **A body's level is a derived quantity: the minimum true rim of its container**, measured on real
  columns. If the real rim is 126, the level is ≤126, whatever the coarse model said.
- **There is no independent water state to preserve or migrate.** Water is recomputed from terrain.
  (This retires the earlier open question "may level correction move existing worlds' water" — there
  is nothing to move; a world's water is a function of its terrain.)
- **Terrain generation must not depend on a water level** except where it deliberately carves (river
  channels), or the two become circular.

## 3. What "physically real" has to mean here

Three invariants, from the directive:

1. **Water rests on static terrain.** No water may occupy a cell whose space is solid, and no water
   column may exist whose bottom is below the real terrain surface at that column.
2. **Water rests on itself.** A settled body has ONE flat surface; water stacks vertically.
3. **Untied water is not representable.** Not "validated after the fact" — *impossible to express*.

Invariant 3 is the load-bearing one. It rules out fixing the depth-buffer gate, which is a
screen-space band-aid that fails precisely when terrain is missing.

## 4. Constraint: we cannot simulate an ocean

Already tried and recorded: growing the CA region to 256×32×256 gave **19 FPS with mass oscillating
531k → 889k → 571k instead of settling**. Brute-force simulation is ruled out **with evidence**. The
existing 64×32×64 `WaterSimulation` is physically correct but bounded.

The resolution: **still water does not need simulating.** A settled body is hydrostatic — a flat
surface at a level set by its container. It needs *representation*, not integration. Simulation is
only required where water is *moving*.

## 5. Proposed architecture — water becomes chunk-resident occupancy

### 5.1 Storage: FULL 3D OCCUPANCY, encoded as per-column spans

⚑**These are not alternatives — this was posed as a choice and that framing was wrong.** The model is
full 3D occupancy; a per-column **run-length encoding** of it is the storage. Water is genuinely
volumetric, and a column's water happens to be contiguous runs, so runs are the natural encoding of
exactly the same information.

**Per column, a list of `(bottomY, topFrac)` spans**, where `bottomY` is the integer cell the water
rests on and `topFrac` is a **float** surface height. Usually 0 or 1 span per column; a cave lake
beneath a surface lake is 2; a flooded multi-level cavern is N.

What this buys, all of which a per-column *height* would lose:
- **Cave lakes and water under overhangs** — multiple disjoint spans in one column.
- **Water resting on water** — stacked spans, and within a span, water resting on itself.
- **Fractional surfaces** — `topFrac` is the sub-cell fill the CA already models, so a settled CA
  state round-trips into storage without being quantised to whole voxels.

What it costs: kilobytes per chunk (≈1 span × 1024 columns), highly compressible by the existing
chunk-blob-v2 path. Compare per-voxel water *objects*: chunks store a cube/subcube/microcube
hierarchy, so an ocean would mean millions of allocations for information that is, by construction,
a handful of runs.

A span is *physical*: its bottom rests on solid terrain or on another span's top; its top is its
body's surface. **Neither field can express water above the ground it sits on.**

### 5.2 Generation writes it, from REAL heights

At chunk generation, per column: if `col.surfaceY < level(x,z)` and the column is connected to that
body, emit a span from `surfaceY+1` to `floor(level)`. This is the line that already exists — now its
result is *stored* instead of discarded.

**Invariant 1 and 3 hold by construction**: the span is defined relative to the column's own real
`surfaceY`, so water above terrain cannot be expressed.

### 5.3 Rendering derives from chunk data

Water surfaces come from chunk spans, not from the coarse level texture. `WaterCellRenderPipeline`
already draws per-cell water surfaces from a surface-cell list; it is fed from the sim today and
would additionally be fed from chunk spans. **Floating water stops being representable at the
renderer too**, because the renderer no longer has a source that can invent it.

The v4 appearance work (turbidity/roughness/SSR) is unaffected — it shades whatever surface it is
given; only the source of that surface changes.

### 5.4 The CA becomes the motion layer only

`WaterSimulation` is unchanged in kind: it seeds from chunk spans when its region moves in, runs
where water is disturbed, and writes settled results back as spans. Digging a channel removes terrain
→ the span's floor drops → the CA flows water in. That is the physical behaviour the directive asks
for, and it is what the CA is already good at.

### 5.5 Levels are DERIVED from terrain (the governing rule, §2b)

If the bake says a basin's spill is 148.9 but the real rim is 126, §5.2 would dutifully fill to
148.9 — tied to terrain per column, but still the wrong body. Under §2b the level is not the bake's
to decide.

**Ocean is easy and needs no flood.** Sea level is a global constant. A column is ocean iff its real
surface is below sea level **and** it is fine-connected to the ocean. Connectivity can be established
incrementally as chunks stream, which is exactly the rule `WaterManager::setOceanBoundary` already
uses for the sim region — a sealed sub-sea cavity correctly stays dry.

**Inland lakes are the hard case,** because a lake's level is set by its outlet, a genuinely global
property. Two tiers:
- the coarse bake **proposes** a candidate basin and an approximate level (cheap over 32 km);
- a **fine flood on real column heights** then decides the actual level — the minimum true rim — and
  the actual extent. The bake is a hint; the fine pass is authoritative.

⚑The unsolved part, stated honestly: a global flood at per-voxel resolution is not tractable (a
32 km world is ~10⁹ columns against the bake's 65,536), so the fine pass has to be **bounded** —
per-basin rather than per-world, seeded from the coarse candidate. Whether a bounded fine flood can
always find the true rim of a large basin without walking the whole thing is the open engineering
question, and it is the one to spike first.

**An attractive alternative worth spiking against it:** use the CA itself as the generator — let
physics settle water against real terrain once, then bake the settled state into spans. "Water is
defined by the terrain holding it" is then true by execution rather than by construction. The known
risk is cost: the 256³ experiment measured 19 FPS, though that was *continuous* simulation, not a
one-time settle with a cached result — a materially different cost profile that has not been measured.

## 6. Order of work

1. **Invariant + red test.** `water_validate` → `rim_leaks == 0` as the acceptance gate. It exists and
   currently fails 606/606, so it is a ready-made red test.
2. **Chunk water spans** — storage, generation, persistence.
3. **Render from spans**, retire the coarse level texture as a placement source.
4. **CA seeds from / writes back to spans.**
5. **Level correction** (§5.5) — the flood on real heights.

## 7. Expected consequence, stated up front

Enforcing this will make lakes **shrink to their true basins, and some baked lakes may largely
vanish** — because the coarse basin was fictional. The world will look correct-but-emptier before
§5.5 makes the levels honest. That is the right order: better to have less water in the right place
than more water in the wrong one.

## 8. Questions — resolved and remaining

**RESOLVED (2026-08-03):**
- ~~Span vs full voxel occupancy~~ — a false choice. **Full 3D occupancy is the model; per-column
  runs are its encoding** (§5.1). Same information, and it keeps cave lakes, stacked water and
  fractional surfaces.
- ~~May level correction move existing worlds' water?~~ — moot under §2b. Water is a function of
  terrain; there is no independent water state to migrate.

**REMAINING:**
- **Can a bounded fine flood find a large basin's true rim** without walking the whole basin (§5.5)?
  This is the first thing to spike — the whole design rests on deriving levels from real terrain at a
  tractable cost.
- **Ocean depth encoding.** Does an ocean column store a span from the seabed, or is deep water left
  implicit below a depth nothing can observe? Affects storage on ocean-heavy worlds only.
- **What happens to `HydrologyMap`'s other consumers.** River carving, seabed materials, flora gates
  and biome selection all read the bake today. Demoting it to a hint must not silently change those.
