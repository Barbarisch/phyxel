# Far-Representation Providers — distant-horizon architecture

> Design doc spun out of the 2026-07-11/12 far-terrain debugging session (LodTest, 2048u).
> Companion to [`LargeWorldScalePlan.md`](LargeWorldScalePlan.md) Phase 5. Captures the
> architecture the far-terrain LOD system should grow into, so the immediate depth-arbiter
> fix lands against a written plan instead of ad hoc. **Status: design only for the provider
> taxonomy (Axis 1-4, provider layer)** — the sole shipped provider today is generator-derived
> far terrain (`FarTerrain*`), still **OFF by default** (`FarTerrainManager::Params::enabled =
> false`, `engine/include/graphics/FarTerrainManager.h`; re-verified 2026-07-21).
>
> **UPDATE (verified 2026-07-21): the "Immediate next step" compositing fix below already
> shipped**, in the *same* commit as this doc (`07ba0a7`, 2026-07-12) — `FarTerrainMesher`
> gained `kBelowSurfaceBias = 0.5f`, a guaranteed geometric push-down applied to **every** ring
> (`yBias = -kBelowSurfaceBias - 0.01*max(0,ring-1)`, `engine/src/graphics/FarTerrainMesher.cpp`
> line ~299; constant in `engine/include/graphics/FarTerrainMesher.h` line 28), not just the
> `-0.01/ring` cross-ring term this doc originally described as ring-1's *only* (zero) bias. A
> regression test (`tests/graphics/FarTerrainMesherTest.cpp`, "below-surface contract") covers
> it. The root-cause narrative below (why the flicker happened) is still accurate as history;
> the "Immediate next step" section's compositing-fix instruction is DONE, not still pending —
> only the provider-layer roadmap (fog fade, `StorageFarProvider`, landmark/water/pop polish)
> remains open.

## The problem this solves

The current design fuses two concerns that should be separate, and that fusion is the
bug users hit (medium-distance "shadow jitter" flicker):

1. **Compositing** — how distant geometry merges with the near real-chunk field.
2. **Provision** — where the distant geometry's data comes from.

Today both live in `FarTerrainManager`, which draws generator-sampled tiles from distance
**0** outward (total overlap with real chunks) and relies on the depth buffer + a coarse
coverage-skip to hide them under real chunks. **(Historical bug narrative — see the shipped-fix
note above.)** At the time this was diagnosed, the near ring (`ring=1`, `startR=0`) added only
the `-0.01·max(0,ring-1)` cross-ring term, which is **0** at ring 1, and `quantizeTop` landed tile
tops *coplanar* with the real surface — so the depth test couldn't resolve the overlap and the
winner flickered per-pixel as the camera moved. Because far tiles are flat-lit and real
chunks are shadow-mapped, the flicker showed up specifically in shadowed areas (dark↔light),
which reads as "shadow jitter." Far terrain OFF → rock-solid → confirmed the fight, not the
shadow map. See the "Compositing" section for the fix — **now shipped as `kBelowSurfaceBias`,
a flat 0.5-unit push-down applied on top of the per-ring term on every ring, including ring 1.**

## Axis 1 — data source (the axis the world-type taxonomy is really about)

A game world's distant horizon can be sourced three ways. This is the primary axis; the
"world types" below are combinations of it plus boundary behavior.

| Source | Where distant geometry comes from | Valid when | Cost |
|---|---|---|---|
| **Generator-derived** | Re-sample the same `WorldGenerator` the real chunks use | World == generator output (unedited procedural) | Cheap, infinite, self-consistent by construction |
| **Storage-derived** | Downsample from the *persisted* world (DB LOD pyramid) | World has diverged from the generator (edits, structures, authored, pre-baked) | Needs a persisted LOD pyramid (see Axis 3) |
| **Backdrop** | Static baked heightmap / skybox / painted horizon | Distant region is decorative and never becomes playable | Cheapest; no per-frame geometry |

**The load-bearing insight: every procedural world becomes a storage world the moment the
player edits it.** Build a castle or dig a canyon and the generator-derived horizon shows
the *original* hill for that region until the real chunk streams in and pops. So a "pure
procedural" world still needs storage-derived far data for edited regions. The honest
target is **hybrid**: storage where the world diverges from the generator, generator
everywhere else. The current single-source (generator-only) system cannot express this and
will always lie about edited terrain at distance.

## World-type taxonomy (data source × boundary behavior)

1. **Bounded playable + immersive horizon.** Finite playable area (arena, walled city);
   generation stops at the boundary but the horizon should still read as continuous terrain
   so immersion isn't broken. → **Backdrop** or generator-derived-past-boundary. Often the
   *right* tool here is fog + skybox, not fake geometry (see Axis 4).
2. **Infinite procedural (Minecraft-like).** World grows as explored; the horizon must be an
   honest preview of what will generate (no faking ungenerated chunks). → **Generator-derived**
   for virgin terrain + **storage-derived** for edited regions = **hybrid**.
3. **Massive pre-generated, hard boundary.** The whole world already exists on disk (edited/
   authored/pre-baked); need to render the real distant horizon cleanly. → **Storage-derived**;
   the generator cannot reproduce it. **This is secretly a storage problem** (Axis 3), not a
   renderer problem.

## Axis 2 — representation fidelity (heightmap vs volume)

The current far terrain is a **2.5-D heightmap** (one surface-Y per column). It *lies about
verticality* — and this voxel engine supports the features it lies about:
- Overhangs, arches, floating islands → a heightmap renders them as solid pillars or drops
  them entirely.
- A distant canyon's interior / the void below → the heightmap shows only the top lip.
- Sub-voxel detail, megaflora, structures → flattened to a height.

So there's a fork by world content: **cheap heightmap** (fine for rolling terrain) vs.
**true coarse-voxel volume LOD** (needed for dramatic vertical worlds — this is the reverted
chunk-downsample track, `LargeWorldScalePlan.md` Phase 5.4). A provider must declare which it
produces; the compositor treats both the same (they just report depth).

## Axis 3 — storage LOD pyramid (makes world-type #3 feasible)

A massive pre-generated world cannot read full-res chunks off disk just to downsample them
for the horizon every frame. It needs a **persisted LOD pyramid**: downsampled chunk mips (a
quadtree/octree of coarse height+color, or coarse occupancy grids) built at *save* time and
streamed like a texture mipmap. This is invisible from the renderer but decides whether
world-type #3 is even affordable. It connects directly to the storage-v2 work
(`LargeWorldScalePlan.md` Phase 1): the DB blob format would gain LOD levels. Until this
exists, storage-derived far terrain is not practical and #3 falls back to generator or
backdrop.

## Axis 4 — atmosphere as an alternative to geometry

"Immersion at a boundary" has two tools, and which one is a per-world call:
- **Extend geometry** to the horizon (providers above) — for genuinely open worlds.
- **Hide the edge with atmosphere** — distance fog that fully occludes the boundary before
  it's visible, + a skybox/backdrop. Cheaper and cleaner for small bounded worlds
  (world-type #1). A bounded arena does not need kilometers of fake terrain.

Fog is *also* required alongside geometry for the far providers, to fade the LOD's last ring
and the near↔far quality step into the sky (hides tile pop-in and the heightmap→real snap).

## Other cross-cutting requirements (don't forget)

- **Distant landmarks/structures** are a *separate need from terrain LOD.* Seeing a castle/
  tower on the horizon and walking toward it is immersion-critical, and a terrain heightmap
  won't render it (it's voxels/structures, not height). Wants its own silhouette/impostor
  provider layered on top of the terrain provider.
- **Water horizon.** A world with a sea needs the distant ocean to extend too, or coastline
  floats in void. Far terrain currently has no far water.
- **The near↔far transition pop.** Quantized far height → full-res real chunk is a visible
  snap on approach. Smoothing (morph / dithered crossfade) or accepting it is a per-world
  immersion decision; world-type #2 implicitly demands it be managed.
- **Lighting consistency.** Far terrain is flat-lit (baked skylight, no shadows). At sunset
  the near field is warm + shadowed and the far field is flat — the mismatch breaks immersion
  at distance. Far providers need at least directional sun tint matching.
- **Origin precision.** A truly massive horizon (tens of km) exceeds world-space float
  precision (>100km wobble is already documented). World-type #3 at full extent needs
  camera-relative rendering.

## The architecture — separate compositing from provision

### Compositing layer (provider-agnostic; the immediate fix)

The renderer merges near real chunks with whatever distant geometry a provider supplies, via
a **single depth arbiter**:

- **Real chunks always strictly win the depth overlap.** Far geometry is drawn after static
  geometry and is guaranteed to report depth strictly *behind* any real chunk surface it
  overlaps (a small guaranteed depth/Y margin on *every* ring, not just rings ≥2). This makes
  the depth buffer the one true gate: it always resolves "what's nearest" and can never
  flicker on a coplanar tie.
- **Coverage-skip is demoted to a pure optimization.** Skipping tiles fully covered by
  resident chunks saves GPU (don't rasterize hidden tiles), but correctness no longer depends
  on it — it can be coarse or even wrong without producing artifacts, because depth handles
  correctness.
- **Chunk distance/frustum/occlusion culling is unchanged** — that's just not-drawing-
  offscreen work, always worth it, not competing with anything.

This collapses the "two competing render-distance gates" into **one depth arbiter + optional
perf skips**, preserves the no-holes property that `startR=0` was chosen for (camera≠player
never opens a gap), and makes the near-ring coplanar fight structurally impossible.

### Provider layer (chosen per-world by world type)

Behind a stable interface (`FarProvider`: "give me coarse geometry / silhouettes for this
distant region around the camera"), one of:

- **`GeneratorFarProvider`** — samples `WorldGenerator`. The current `FarTerrainMesher`.
  World-type #2 (virgin terrain).
- **`StorageFarProvider`** — reads the DB LOD pyramid (Axis 3). World-type #3, and the
  edited-region half of #2's hybrid.
- **`HybridProvider`** — storage where the world diverged, generator elsewhere. The real
  open-world case.
- **`BackdropProvider`** — static baked heightmap / skybox + fog. World-type #1.
- **`LandmarkProvider`** (optional, layered) — structure silhouettes/impostors on top of
  terrain.

All providers feed the same compositor; the compositor doesn't care which produced the
geometry. World type selects the provider (via `game.json` far-terrain config).

## Immediate next step

~~Land the **compositing fix** (depth arbiter: guaranteed real-chunk depth win on all rings)
against this doc~~ — **DONE (verified 2026-07-21, shipped in `07ba0a7` alongside this doc as
`FarTerrainMesher::kBelowSurfaceBias`).** Remaining, in priority order: fog fade (Axis 4, also
needed by every provider) → `StorageFarProvider` + DB LOD pyramid (unlocks #3 and hybrid #2) →
landmark/water/pop polish. Volume LOD (Axis 2) is the reverted Phase-5.4 track, revisited only
for vertical worlds. (Far terrain is still off by default even with the compositing fix landed —
turning it on is a separate decision, not blocked on more code here.)
