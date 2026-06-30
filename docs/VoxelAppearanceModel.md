# Voxel Appearance Model — Material / Tint / State / Texturing

> **Status:** Phases 1–3 shipped (per §7 phase plan); Phase 4 pending. Canonical
> reference for decoupling a voxel's *appearance* from its *substance*. Born out of
> the Barony `.vox` import work (`tools/vox_import.py`, `tools/gen_vox_palette.py`),
> which exposed that "material" is currently overloaded to mean both physics and looks.
> Phase 1 (per-voxel tint) and Phase 2 (authored state surfaces) landed earlier; Phase 3
> (planar projected textures — rugs/paintings/banners) shipped 2026-06-30 (MVP).

## 1. The problem

Today a voxel is described by a single `material` (an index into `MaterialRegistry`,
data-driven from `resources/materials.json`). That one field is forced to carry **two
unrelated jobs**:

1. **Substance** — physics & behavior: mass, friction, restitution, `bondStrength`,
   `breakForceMultiplier`, `metallic`, `roughness`, emissive.
2. **Appearance** — a fixed 6-face texture set.

Because the two are welded together, you cannot have "a red *wooden* thing" without
either (a) inventing a new material (`vox_21`) that has a red look but *generic*
physics — losing the wood identity — or (b) snapping the color to the nearest
existing textured material, which turns everything grey. The Barony import hit
exactly this: 8,000+ source colors collapsing into ~29 mostly-stone materials.

We also want **richer surface detail than the voxel grid can express** (a rug with a
pattern across the whole rug; a book with a wrapped cover) **without** inventing
smaller voxels — sub-microcube geometry would tank the renderer (face count is
already the #1 perf issue, see `docs/RenderOptimization.md` #40). The answer is to
get as close as the voxel grid allows and **elevate it with textures**.

## 2. The model: four orthogonal axes

Split the single `material` into four independent axes. A voxel/face becomes
`(substance, tint, surface, state)`.

| Axis | Means | Drives | Cardinality |
|------|-------|--------|-------------|
| **Substance** (material) | what it's *made of* | physics, behavior flags (flammable, conductive, porous), the *default* surface | few, meaningful (Wood, Stone, Metal, Flesh, Cloth, Glass, Paper, Water…) |
| **Tint** | a color multiplier | appearance only — multiplies the surface albedo | a shared palette (≤256) |
| **Surface** | *how* it's textured | which texture + UV projection mode | per material default, overridable per object |
| **State** | a runtime/authored modifier | visual modifiers (emissive, hue shift, gloss, particles) **and** physics deltas | small enum per material |

Worked examples:

- **Campfire** = `Wood` substance + brown tint; the glowing embers are the *same*
  `Wood` voxels in **state = flaming** (emissive + warm shift + later particles).
  Not a separate "burning wood" material.
- **Rug** = `Cloth` substance (soft physics) + a **projected** woven-pattern texture
  spanning the whole rug.
- **Book** = `Paper`/`Wood` substance + a **box-unwrapped** cover texture.
- **Imported Barony chair** = `Wood` substance + per-voxel tint from the palette.

### Why state, not a material per state

State **transitions at runtime** (dry Wood → ignites → smolders → chars → ash) and
shares ~90% of the substance's physics. A material-per-state can't transition and
explodes the count (Wood × states × tints). Rule of thumb:

> **Transitions + shares physics → state. Genuinely different substance → material.**
> (Ice vs Water is the gray zone — fine to keep as separate materials for simplicity.)

## 3. What the engine already has (the hooks)

This is *not* a from-scratch system — several pieces exist:

- **Data-driven, auto-sized texture array.** `AtlasManager` builds a `sampler2DArray`
  whose `layerCount = registry.getTextureCount()` — adding textures just grows it
  (BC7, hash-cached). No fixed shader texture cap. Mixed 512/1024 res classes.
- **Auto flat normals.** Any face without an `<albedo>_nr.png` sidecar gets a flat
  normal+roughness layer auto-generated. One albedo PNG suffices for a flat color.
- **Per-voxel visual modifiers already exist.** `InstanceData.reserved` packs
  `bit0 emissive, bit1 transparent, bits2-9 alpha, bit10 mirror, bits11-14 damage`.
  Damage cracks are *already* a per-voxel state-like visual modifier — the exact
  pattern **state** will generalize.
- **Emissive + colored block light.** Emissive materials flood-fill colored light
  from `physics.colorTint` (`ChunkRenderManager`). "Glowing/burning" already has
  working light infrastructure.
- **CPU-controlled per-face UVs (kinematic path).** `KinematicVoxelManager` computes
  `uvOffset`/`uvScale` per face on the CPU (40B `KinematicFaceData`,
  `kinematic_voxel.vert`). This is the natural home for projected textures.
- **`colorTint` per material** already in `materials.json` — the seed of the tint axis.
- **The vox palette.** `tools/gen_vox_palette.py` already produces a 48-color,
  art-derived palette; it becomes the **tint palette** rather than 48 fake materials.
- **State subsystems to hook into.** `HazardSystem` (fire/hazards), `WaterManager`
  (already models "wet" columns).

### The three voxel render paths (know which you're touching)

| Shader | Use | UV method | Free space |
|--------|-----|-----------|------------|
| `static_voxel.vert` | chunk-baked terrain & **placed static props** (`PlacedObjectManager` → `ChunkManager`) | GPU decodes grid pos → per-cube tiling | `packedData` has **11 free bits** |
| `dynamic_voxel.vert` | GPU particle debris | GPU decode from `localPosition` | 64B, roomy |
| `kinematic_voxel.vert` | doors, furniture (activated), fragments | **CPU pre-computes `uvOffset`** | 40B `KinematicFaceData` |

The static chunk path is the **fragile, perf-critical** one (greedy-meshed, winding-
sensitive — see `reference_render_pipeline_internals`). Minimize changes there.

## 4. Central architectural decision: two tiers

To keep the rich appearance work off the fragile static path, formalize two tiers:

- **Tier 1 — Bulk/terrain.** Chunk-baked, material-tiled, greedy-meshed. Max perf.
  Optionally gains *tint* later; otherwise unchanged.
- **Tier 2 — Decorated props.** Rendered as **kinematic voxel groups** (CPU UV
  already), carrying the **full appearance model**: tint, state, projected textures.
  For *hero / detail objects* — imported Barony props, rugs, books, signs, paintings,
  furniture.

Imported decorated objects route to **Tier 2**. This localizes nearly all new work to
the kinematic pipeline and leaves the static chunk renderer mostly alone.

**Tension to manage:** Tier-2 groups are not greedy-merged, so each is its own draw
and microcube-dense props add faces — the existing #1 perf issue. *Projected textures
are the mitigation, not just a feature:* detail carried by the texture means **fewer
voxels needed**, which directly reduces face count. Better textures → fewer voxels →
less density pressure. Reserve Tier 2 for objects that earn it; keep bulk in Tier 1.

## 5. Data model & format changes

### Voxel record
```
voxel = {
  material : MaterialID        // substance → physics (unchanged meaning)
  tint     : uint8             // index into global tint palette; 0 = white/no-tint
  state    : uint8             // index into material's state table; 0 = normal
  // surface is per-object/template metadata, not per-voxel (see §6.3)
}
```

### `.voxel` template format (additive, back-compatible)
- Per-primitive optional trailing tokens, defaulting when absent:
  `M cx cy cz sx sy sz mx my mz  Wood  tint=#b0552c  state=flaming`
- Object-level header metadata for surface/projection:
  `# surface: texture=rug_persian.png projection=planar axis=Y`
- The importer (`vox_import.py`) emits `material + tint=` per primitive (see §6).

### Render data
- **Tint (chunk path):** pack an **8-bit tint index** into `packedData`'s 11 free
  bits; shader does `albedo *= tintPalette[idx]` (idx 0 → vec3(1) → no-op). Global
  tint palette as a small UBO/SSBO (≤256 × vec3), fed by `vox_palette.json`.
- **Tint (kinematic/dynamic):** add a tint index/word to `KinematicFaceData` /
  `DynamicSubcubeInstanceData`; same multiply.
- **State:** start by reusing the `reserved` modifier pattern (emissive/alpha/gloss
  bits); a per-state visual descriptor table maps `state → {emissiveBoost, hueShift,
  roughnessDelta, particleEmitterId}`.
- **Projected texture (Tier 2 only):** extend `KinematicVoxelManager`'s UV step to
  compute object-space UV from the face's position within the group extent, instead of
  per-cube tiling.

## 6. Importer integration (Barony & future voxel sources)

1. **`vox_import.py`** maps each source color to **(substance, tint)**:
   - substance by heuristic on the color family + **per-model `--map` overrides**
     (e.g. `--map 5-9=Flesh,20-24=Metal`),
   - tint = nearest entry in the global tint palette.
   - Replaces today's `--matset vox` (map-to-fake-materials) with `material + tint=`.
2. **`gen_vox_palette.py`** output becomes the **global tint palette** (not 48
   materials). The current 48 `vox_*` materials in `materials.json` are then retired.
3. Glowing source indices import as substance + **`state=flaming`** (or emissive tint).

## 7. Phase plan (smallest valuable slice first)

- **Phase 1 — Per-voxel tint (keystone).** Decouple color from substance. Tint index
  in the voxel record + format + the three shaders' albedo multiply + global tint
  palette UBO. Importer emits `(material, tint)`. **This alone fixes imports *and*
  the aesthetic-override wish**, and everything else layers on it.
  - *1a (lower risk):* tint on the **kinematic (Tier 2)** path first — imported props
    route here, so imports benefit immediately without touching the chunk shader.
  - *1b:* extend tint to the static chunk path (enables tinted terrain/bulk).
- **Phase 2 — Authored static state.** A `state` field + visual-modifier table,
  reusing emissive/damage hooks. Ship `flaming` (emissive+warm) and `wet`
  (darken+gloss) first. No simulation yet.
- **Phase 3 — Planar projected textures (Tier 2). ✅ SHIPPED (2026-06-30, MVP).** One
  image across a flat object along its dominant axis. Pure CPU UV math on the kinematic
  path. Unlocks rug/painting/banner/mosaic. Authoring: `# surface: texture=<material>
  projection=planar axis=<x|y|z>` header + the texture asset (a texture-carrier material,
  like `surface_test`/`burning_wood`). Implementation:
  - `KinematicFaceData` grew a per-axis `vec2 uvScale` (40→48B; attribute loc 5 in
    `KinematicVoxelPipeline` + `kinematic_voxel.vert` `uv = baseUV*inUvScale + inUvOffset`).
    Non-projected faces set `uvScale = vec2(scale.x)` → byte-identical legacy behavior.
  - `KinematicVoxelManager::buildFaces(voxels, KinematicSurface)`: faces whose normal is
    on the surface axis project across the whole object extent (same per-face flip
    convention) and swap to the surface texture index; all other faces tile per-cube.
  - Parsed in `ObjectTemplateManager` → `VoxelTemplate::surface`; resolved + carried to
    the kinematic group in `ItemPropManager` (both `spawnProp` + `rebuildFromPlacedObjects`).
  - Validated: `tests/core/KinematicSurfaceProjectionTest.cpp` (L2 — UV slices tile the
    object extent, non-square + single-cube + legacy-guard) and L4 runtime (`rug_test`
    item → one continuous image across a 2:1 subcube rug).
  - **Still open:** real art (placeholder `rug_test.png` only — track in
    `docs/MaterialTextureNeeds.md`); a dedicated surface-texture registry (MVP reuses the
    material array); vertical-axis (painting/banner) runtime check; books = Phase 4 box-unwrap.
- **Phase 4 — Dynamic state + box-unwrap textures.** State *transitions* + physics
  deltas (fire spread weakening bonds; wet propagation) via `HazardSystem`/
  `WaterManager`; and skin-style box unwrap for books/crates/signs.

## 8. Risks & open questions

- **Fragile static path.** Phase 1b touches `static_voxel.vert` + `InstanceData`
  packing — do it red-before-green with visual verification; 1a avoids it.
- **Tier-2 draw-call / face budget.** Quantify a cap on simultaneous decorated props;
  lean on projected textures to keep voxel counts down.
- **Tint storage in chunks.** Per-voxel tint byte vs a sparse side-table — measure
  memory on a full chunk before committing.
- **Authoring pipeline.** Projected textures need *art*. Could BlockSmith generate
  rug/book/sign textures? Track needed art in `docs/MaterialTextureNeeds.md`.
- **Substance taxonomy.** Define the canonical substance list + each one's state
  table and behavior flags (new section of `materials.json`?).
- **Lighting synergy (opportunity, not risk).** Projected textures still receive baked
  per-voxel light and can carry `_nr` normal maps for woven/embossed depth — free win.

## 9. Related
- `tools/vox_import.py`, `tools/gen_vox_palette.py` — the import + palette tooling.
- `docs/TextureSystemOverhaul.md` — mixed-res BC7 `sampler2DArray` design.
- `docs/RenderOptimization.md` (#40) — sub/microcube face-count problem (why not smaller voxels).
- `reference_render_pipeline_internals` (memory) — fragile winding/pass-order facts.
- `docs/MaterialTextureNeeds.md` — track missing textures/art.
