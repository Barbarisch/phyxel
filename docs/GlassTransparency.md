# Glass is not transparent — investigation

**Status:** OPEN, cause UNKNOWN, **deferred until the crack system (`VoxelDamageVisualization.md`)
is finished.** Nothing here is started.

**Symptom:** glass does not render see-through in the editor. Reported by the reviewer on
2026-09-22 while reviewing the damage-crack work, and again on 2026-09-23 after a second wrong fix.

> ⚠️ **STANDING INSTRUCTION: do not report this fixed without the reviewer confirming it visually.**
> It has been reported fixed twice and was wrong both times. The second was worse than the first:
> a milky pane with the horizon faintly visible through it was read as "transparent". A reading of
> the pixels is not a reading of whether it looks right, and on this defect specifically the
> author's judgement has been demonstrated unreliable.

---

## 1. What is RULED OUT

**The crack rendering is not the cause.** Two fixes were attempted (`58b00dfd`, `03e68fa9`) and both
touched *only* the crack block in `voxel.frag` — softening the crack darkening on transparent
materials, then excluding transparent materials from cracking altogether. Neither could have caused
a transparency bug and neither fixed one. Both are reverted (`2ee675d6`).

**Cracks on glass are not the problem and must not be removed again.** The reviewer confirms they
looked good. The exclusion added in `03e68fa9` deleted a working feature for no reason.

---

## 2. What is NOT established — and this is the next step

**Whether the defect belongs to this branch at all.**

A baseline was built: the only three files on `feature/voxel-damage-cracks` that could plausibly
reach glass rendering were reverted to `origin/main` —

- `shaders/voxel.frag`
- `engine/src/core/AtlasManager.cpp`
- `engine/src/vulkan/VulkanDevice.cpp`

— the engine rebuilt, and a plain undamaged glass wall captured:
**`screenshots/screenshot_20260923_072602_473.png`**.

**That capture has not been judged by the reviewer.** Until it is, it is unknown whether glass
renders correctly without this branch's changes, and the investigation cannot be pointed in either
direction. **Start here; do not write code first.**

| baseline verdict | what it means | where to look |
|---|---|---|
| glass renders CORRECTLY | the break is on this branch | §3 — bisect the three files |
| glass is ALSO broken | predates the branch entirely | own investigation against `origin/main`; nothing in the crack work is implicated |

---

## 3. Prime suspect, IF the break is on this branch

**P5's material-props stride change** (`330ee050`).

P5 widened the per-material props SSBO from **one `vec4` per texture layer to two**:

```
before:  props[gi]       = (metallic, roughness, emStrength, emThreshold)
after:   props[gi*2 + 0] = (metallic, roughness, emStrength, emThreshold)
         props[gi*2 + 1] = (crackStyle, 0, 0, 0)
```

`AtlasManager.cpp` writes it at the new stride, `voxel.frag` reads `textureUVs[gi * 2u]`, and
`VulkanDevice.cpp` doubled the buffer allocation to match.

**Why it is the suspect:** any consumer still assuming stride 1 now reads *another material's*
properties. That is exactly the shape of "one material renders wrong". A grep found no other shader
indexing `textureUVs[]` — `transparent_voxel.frag`, `mirror_voxel.frag`, `far_terrain.frag` and
`far_tree_mesh.frag` declare the block but only read the header counts — **but that grep was not
verified by experiment**, and the declared-but-unused array still fixes the binding layout.

**Bisect procedure (one file at a time, rebuild and capture between each):**
1. `voxel.frag` alone at branch state, other two at `origin/main`
2. `AtlasManager.cpp` alone at branch state
3. `VulkanDevice.cpp` alone at branch state

The one that reproduces it is the cause. Three builds; each is a few minutes.

---

## 4. Things known about the render path (gathered, not yet used)

- **`voxel.frag` has no transparency discard.** Glass is drawn in the main pass and
  `outColor = vec4(color, textureColor.a)` carries its alpha.
- **There is also an OIT pass.** `transparent_voxel.frag` discards anything without flag bit 1,
  reads material alpha from flags bits 2–9 (`(flags >> 2u) & 0xFFu`), and writes accumulation +
  reveal targets. `RenderCoordinator::renderTransparentGeometryOIT` runs it, gated on a cached
  per-chunk `hasTransparentVoxel()` flag refreshed on `recomputeRenderFlags()`.
- **That flag is computed from MATERIAL alpha**, not from damage or instance bits
  (`Chunk.cpp:416-439`), so damage cannot switch it off.
- **Instance `reserved` bit allocation:** 0 emissive · 1 transparent · 2–9 quantized alpha ·
  10 mirror · 11–14 damage · 15 `varied`. Damage bits do not overlap the alpha field.

None of the above has been tested against the live defect — it is context for whoever picks this up,
not a conclusion.

---

## 5. Rig that produces a usable answer

Both panes in **one frame**, so the comparison is not across captures with different scene state:

- a glass wall with **grass and sky behind it** (something clearly identifiable through the pane),
- a second glass wall beside it in the same frame if comparing two states,
- **no backing wall** unless deliberately testing occlusion — an earlier run misread a Stone wall
  seen *through* the glass as the glass itself being opaque,
- the reviewer judges the frame.

`tools/damage_ladder_rig.py` is the crack rig and is **not** suitable: it builds Stone.

---

## 6. Why this is deferred

The crack system (`VoxelDamageVisualization.md` §16) has P4 and the §6.2 seam guard outstanding.
Glass transparency is a visible defect but blocks none of that work, and the crack rendering is now
confirmed correct on opaque materials and on glass. Finishing the crack system first keeps the two
investigations from contaminating each other — which has already happened once, when a transparency
bug was mistaken for a crack bug and "fixed" twice in the crack code.
