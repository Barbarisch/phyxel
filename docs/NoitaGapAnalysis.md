# Noita in 3D — gap analysis of the Phyxel engine

**Status: ANALYSIS 2026-11-03.** Read-only audit, nothing built. Sources:
- four parallel code audits (materials, destruction lifecycle, fluids/fire/particles, bodies and
  gameplay), with five load-bearing claims spot-checked against source by hand (§3, marked ✔);
- the owner's reference: a Noita making-of video (youtube `DCAMRAbMq7c`, transcript supplied by
  the owner), cited as **[V]**.

Companion plan: [`CollisionProxyPlan.md`](CollisionProxyPlan.md) (complex shapes → capped proxies).

**Owner goal:** Noita is the gold standard for feel and physics, and the aim is to bring as much
of its 2D "every pixel is simulated" model into this 3D "every voxel is a material" engine.
Water may not fit the model exactly.

---

## 1. The Noita model, as principles

What the video says Noita actually *is*, reduced to the principles we measure against:

| # | Principle | From [V] |
|---|---|---|
| **N1** | **Local rules are the core.** Sand: fall, else diagonal. Liquid: also sideways. Gas: same, upward. | "about 95 percent of Noita's technology" |
| **N2** | **Every material has a phase and properties.** Fire spreads through wood and oil; water evaporates; acid eats terrain. | opening |
| **N3** | **Materials react with each other.** Pairwise rules give the emergent chains. | "two liquids mix together and they have some sort of reaction" |
| **N4** | **Matter is conserved.** Pixels detach, fly, land and rejoin the world. Nothing silently vanishes. | implied by the single shared grid |
| **N5** | **Bodies are made of tracked pixels.** Each pixel knows its body, material and local position. The shape is recomputed when pixels die (marching squares → RDP → triangles). A cut makes two bodies. | rigid-body section |
| **N6** | **One shared grid, two-way coupling.** Per frame: lift body pixels out, step the sand sim, step Box2D, write the bodies back. Material in the way is pushed out with velocity, which gives splashes. | rigid-body section |
| **N7** | **Only simulate what changes.** 64×64 chunks, dirty regions, a 4-pass checkerboard across threads, streaming. The world keeps simulating away from the player. | scale section |
| **N8** | **Readable, consistent rules under the chaos.** Oil lamps *always* explode. Status icons show wet or oiled. Rigid bodies can't damage the player, because physics glitches felt random. | design section |
| **N9** | **Creatures and objects are material too.** Enemies spill material, lamps spill oil, a dead body falls and smashes a lamp. | the oil-lamp anecdote |

Design lessons from [V] that bear directly on Phyxel (§6): a persistent world lets the player
ruin it; fully simulated buildings collapse into rubble; a dig-anywhere tool can trivialise play.

---

## 2. Scorecard

✅ built · 🟡 partial · ❌ absent. "Leverage" = how much Noita-feel it buys per unit of work.

| # | Phyxel today | Grade | Leverage |
|---|---|---|---|
| N1 local rules | Static voxels are **inert**. The only cellular automaton is water. No powder falls or piles; no gas. | ❌ | **Highest** |
| N2 phases & properties | Materials are fixed-property solids: mass, friction, restitution, buoyancy, break toughness, render. No phase, flammability, temperature or conductivity. | 🟡 | High |
| N3 reactions | None. A per-voxel state byte (flaming/smoldering/charred/wet) exists, but it is **authored only and read only by the renderer**. | ❌ | High |
| N4 conservation | Break → GPU debris works. But debris **never re-enters the grid**: it expires after 25 s even when settled. Shatters spawn a fraction of the volume. Pool-full spawns are dropped uncounted. | 🟡 | **High, cheap** |
| N5 bodies of voxels | Furniture, items and fragments are per-voxel-material kinematic sets. The shape is greedy boxes, not a proxy. Fracture exists (impact and API shatter). Creatures have **no material voxels** (colour only). | 🟡 | High |
| N6 two-way coupling | Movers push debris, debris pushes characters (2 frames late), debris and water are two-way. CPU bodies don't feel debris; bodies don't displace any material except water. | 🟡 | Medium |
| N7 only simulate change | Debris sleeps and wakes. Water is a 64×32×64 box following the player. Blasts re-mesh synchronously and outside the frame budget. No world-wide active-region sim. | 🟡 | Enabler |
| N8 readable rules | No camera shake, hit-stop, per-material impact sound or status effects. A fireball is a kinetic blast like any other. | ❌ | Medium, cheap |
| N9 creatures as material | Creatures are HP-only puppets: no holes, no material spilled, blasts can't launch characters. Derez is a scripted dissolve. | ❌ | High |

**Where Phyxel is already strong** (and ahead of Noita in 3D):
- a real GPU rigid-body solver for 10k debris pieces (AVBD) that rests on the voxel occupancy;
- energy-vs-toughness destruction with shielding and cube→subcube→micro shatter tiers;
- coherent rigid collapse with impact fracture;
- sub-voxel WaterCore, two-way with debris;
- chop kerfs;
- three voxel scales.

Bluntly, though: **we've built Noita's rigid-body half well and almost none of its material half.**
N1–N3 are what the video calls 95 % of the tech.

---

## 3. Findings by area (file:line; ✔ = hand-verified 2026-11-03)

### 3.1 Materials (N2, N3)
- **Schema:** `MaterialRegistry.h:32-98`.
  - Physics fields: mass, friction, restitution, damping, bondStrength, buoyancy.
  - `break` fields: toughness, s1, s2, absorption.
- **`break` profiles are rare:** only **7 of 108** materials have one; the rest fall back to
  `bondStrength×120` (`DamageSystem.cpp:34-51`).
- **Dead fields:** `angularVelocityScale` (loader only); `breakForceMultiplier` (uploaded to the
  GPU, never read).
- **Phases:** none. Sand and Gravel are worldgen-only words (`WorldGenerator.cpp:1656-1671`).
- **State byte** (`VoxelTemplate.h:28`; subcubes/micros only, cubes carry none): set by template
  parse or add-voxel args, read by the burning texture and emissive light only.
  `materials.json:2633` says "Physics unused". Nothing writes state 4 (wet).
- **Damage type ignored ✔** `DamageSystem.cpp:152` (`/*damageType*/`): fire, acid and lightning
  are all the same kinetic blast.
- **Characters don't feel materials:**
  - No footsteps. No statuses: `ConditionSystem` is D&D only.
  - Terrain friction never applies: contact friction uses body A's, and B's only when B is a body
    (`VoxelContactSolver.cpp:457`). So **Ice isn't slippery** to anything but debris.

### 3.2 Destruction lifecycle (N4, N7)
- **Debris exits by expiry or gather only ✔.**
  - `spawnBreakDebris` sets `lifetime = 25` (`DamageSystem.cpp:108`).
  - The CPU ages *every* active slot, sleeping or not (`GpuParticlePhysics.cpp:1274-1285`). A
    settled pile vanishes after 25 s.
  - No code path re-bakes debris to chunk voxels.
- **Mass is not conserved:**
  - A subcube shatter spawns 12 of 27 pieces (`DamageSystem.cpp:354`).
  - A micro pulverise spawns 24 of 729 (`:367`).
  - Leaves spawn none (`:339`).
  - `MAX_DEBRIS` = 4000 per blast; past it, voxels vanish (§15.1, "silent geometry loss").
- **Pool full = silent ✔.** `queueSpawn` logs and ignores the piece, while `spawnBreakDebris` has
  already returned true and the caller counted it as spawned (`GpuParticlePhysics.cpp:1146`).
- **Debris mass ignores piece size ✔.** `solver_sync_in.comp:129` uses `mat.mass` directly, so a
  1/9 micro of stone weighs the same as a full cube. Relative masses in every collision are off by
  up to 729×, which is likely a real contributor to "feel".
- **Restitution is read into the body but no solve pass uses it.** Nothing bounces. `GAMMA`
  doubles as damping; 8 solver iterations, never tuned.
- **Collapse:**
  - `collapseUnsupported` (`DamageSystem.cpp:1668`) runs only from `applyDamage(collapse=true)`
    and chop kerfs. **Player B-key breaks never trigger collapse ✔**
    (`VoxelManipulationSystem.cpp`: no call).
  - Flood caps: 3000 terrain / 20000 tree cells. Larger severed pieces count as supported and
    float (U3).
- **Coherent fragments:**
  - Spells turn them on. `/api/damage/apply` defaults off, and **shipped games
    (`DebrisRuntime::applyDamage`) never pass it, so they always scatter**.
  - Fragments never retire or re-bake: "no re-bake" is a locked decision
    (`DestructionSystemV2.md:407`).
- **Scale:**
  - Blast radius is unclamped; scan is O(R³) with a shielding ray-march per cell.
  - The post-blast re-mesh drains synchronously, bypassing the 6 ms budget (`DamageSystem.cpp:401`).
  - Mesh costs 2–13 ms per chunk.
  - 2–3 big blasts fill the 10k pool.

### 3.3 Fluids, gas, fire (N1, N2, N3)
- **Live water** is the old 1 m cellular automaton, in a 64×32×64 box that follows the camera.
  It does flow into dug or blasted holes.
- **WaterCore** (sub-voxel; GPU or CPU; FLIP option; owner-approved still pond and creek clips) is
  **debug-route only**: real-time off by default (`WaterCoreManager.h:464`).
- **Other liquids, density layering:** none. WaterCore §23 has a "one liquid per volume, no
  mixing" slime sketch.
- **Gas, smoke, steam:** none. Mist and splashes are VFX.
- **Fire:**
  - VFX on *authored* flaming voxels (`FireEmitterManager`).
  - No spread, fuel, ignition or extinguish.
  - `campfire` and `torch` are particle fountains.
- **VfxSystem** is "no physics collision" (`VfxSystem.h:158`). Projectile, beam and field effects
  never touch voxels.
- **Lights** from VFX and flaming voxels exist; the scene cap is 32 lights.

### 3.4 Bodies, creatures, gameplay (N5, N6, N8, N9)
- **Furniture:**
  - Per-voxel material. Activation happens on interaction.
  - `shatter()` is local around the contact point (`DynamicFurnitureManager.cpp:549`).
  - It is reachable only through the `shatter_furniture` API; **nothing shatters on impact**.
- **Items:** never damageable. CPU caps: 16 furniture, 6 items.
- **Creatures:**
  - `RagdollPart` = scale, colour, name; **no material**.
  - Damage is HP plus knockback plus animation. A hit spawns nothing; there's no dismemberment.
  - `/api/physics/impulse` and blasts don't touch characters.
  - No ragdoll.
- **Spells:** combat spells are HP-only. `DebrisRuntime::spellBlast` has **no callers**.
- **Feel:**
  - No camera shake, hit-stop or screen effects.
  - One generic `debris.impact` sound, capped at 3 per frame.

---

## 4. The central architectural gap — and the 3D cost reality

Noita has **one grid where every cell is a material with a phase**, stepped by local rules
wherever something is active. Phyxel has **an inert static grid** with three separate moving
systems bolted beside it: GPU debris, CPU rigid bodies, water. Each is good. None of them is "the
material steps itself."

**Why it can't be a straight port:** cost grows with the cube of the resolution.
- A 64×32×64 region at 1 m is 131k cells, already the water box.
- The same region at subcube resolution is 3.5M cells, and at micro 96M.
- Noita's whole active screen is a few hundred thousand pixels.

**The 3D design has to choose:**

1. **The simulation resolution** (the "pixel"). Pure cubes are too coarse for Noita feel; micro
   everywhere is impossible. Likely answer: **subcube (⅓ u) cells, only in active regions**, with
   cubes as sleeping bulk that "wakes" into subcubes when disturbed.
2. **Activity tracking (N7)**: per-chunk dirty flags plus sleep, so a settled dune costs zero. The
   GPU is a natural fit: checkerboard or Margolus-block passes are textbook GPU CA, and we already
   run 15-pass compute solvers per tick.
3. **The particle ↔ grid handshake (N4).** Noita's pixels leave the grid as particles and re-enter
   when they settle. We already have both halves: GPU debris and the static grid. We lack the
   re-entry.
4. **Water's relationship:**
   - (a) keep WaterCore as the liquid solver and add other liquids to it; or
   - (b) treat water as one more cellular-automaton liquid phase.

   [V]'s own warning applies: liquid fidelity is where Noita is crudest. Recommendation: keep
   WaterCore for water bodies, and let the material CA handle *small* liquid volumes (spilled oil,
   blood, acid) that interact with powders and fire.

---

## 5. Roadmap, ordered by leverage per cost

Each tier is independently shippable and each item follows the usual gate: design-check,
red-before-green, stress phase, auditor.

### Tier 0 — honesty and physics bugs (small, high value, do first)
1. **Debris mass scaled by piece volume** (`solver_sync_in.comp:129`). Re-run
   `debris_settle_bench.py`; this changes every collision.
2. **Restitution wired into the solve** (P3), then **iteration count tuned** (R2).
3. **Pool-full refusals counted and surfaced.** Per-blast `MAX_DEBRIS` loss counted, not silent.
4. **`damageType` honoured.** At minimum it routes to a per-type response table. Wire
   `spellBlast` into combat spells.
5. **Terrain material friction applies** to bodies and characters (Ice slips).
6. **B-key breaks run the support flood** (a severed overhang falls).
7. **Shipped games pass `coherentFragments`** like spells do (parity).

### Tier 1 — conservation (N4): debris rejoins the world
8. **Settled debris re-bakes into the static grid** as subcubes or micros at its rest pose,
   snapped. It no longer expires. The pool becomes a working set, not a graveyard.
9. **Shatters conserve volume:** spawn what fits the budget, and re-bake or represent the rest,
   never drop it.
10. **Coherent fragments retire to static voxels** (U5). This reverses the locked "no re-bake"
    decision, so it's the owner's call (§7).

### Tier 2 — phases and the local-rules core (N1, N2)
11. **`phase` added to `MaterialDef`:** `static | powder | liquid | gas`, plus density and
    angle-of-repose fields.
12. **GPU material CA, powder first:**
    - sand, gravel, snow, ash and dirt-rubble fall and pile in active regions;
    - checkerboard passes, per-chunk dirty and sleep;
    - writes back to chunk voxels and occupancy through the existing edit paths.
13. **Unsupported powder falls as powder**, not rigid debris (a sand overhang slumps).

### Tier 3 — state, heat, fire, reactions (N2, N3)
14. **Promote the state byte to simulated state:** burning, smoldering, charred, wet, plus a
    small per-cell heat value in active regions.
15. **Flammability and burn data per material.** Fire spreads to flammable neighbours, consumes
    fuel (wood → charred → ash *powder*), emits light (watch the 32-light cap) and smoke.
16. **A data-driven reaction table** in `materials.json` (`A + B → C + D`, with
    probability/rate). First rules: water+fire → steam + extinguish, and burning → ash.
17. **Gas phase:** steam and smoke rise, are blocked by walls and dissipate.
18. **Small-volume liquids (oil, acid, blood)** in the material CA. Acid dissolves by toughness;
    oil is flammable.

### Tier 4 — bodies and creatures are material (N5, N6, N9)
19. **Collision proxies** for every body: [`CollisionProxyPlan.md`](CollisionProxyPlan.md).
20. **Impact-driven shatter** for furniture and items (shatter already exists; wire it to contact
    impulse). Lamps spill oil: N8 says *always*.
21. **Creatures get material voxels:** per-part material, damage removes voxels, wounds spill
    liquid (blood), limbs can detach as bodies. Reconsider derez in this light.
22. **Blasts and impulses act on characters** (launch, knockdown).
23. **Bodies displace powder and liquid** — the [V] lift-step-write-back loop, which is where
    splashes come from.

### Tier 5 — readability and feel (N8), cheap and parallel to everything
24. Camera shake and hit-stop; per-material impact and break sounds.
25. **Status effects from materials:** wet, oiled, burning, with HUD icons ([V]: the icons made
    players *believe* the simulation).
26. **Consistency rules:** rigid bodies don't damage the player; key interactions are
    deterministic.

### Tier 6 — scale (N7), required before Tiers 2–3 go world-wide
27. **Budgeted, spread re-mesh after blasts**; clamp blast radius; edit queue.
28. **World-wide active-region simulation** of the material CA: a sleep/wake map; regions far from
    the player step at a lower rate or freeze.

---

## 6. Design lessons from [V] that apply to Phyxel specifically

- **Persistence vs ruin.** Noita escaped "the player flooded the portal with lava forever" through
  permadeath. Phyxel *persists* worlds (SQLite) and builds towns with WorldForge. Pick one per
  game:
  - roguelike resets;
  - regenerating / healing worlds, which the generator could re-derive since appearance is a pure
    function of world position plus state;
  - protected zones.

  This must be decided before Tier 3 fire can burn down a generated town permanently.
- **Collapsing buildings ruin levels.** WorldForge settlements plus full structural collapse is
  exactly the prototype [V] says turned into "a pile of rubble at the bottom of the screen."
  Material budgets (support strength, fire-resistant stone) are a design tool, not just a physics
  number.
- **Dig-anywhere trivialises play.** The B key and blasts are unlimited today. In a game, digging
  power is something to grant, not a default.
- **Chaos on top of consistency.** Every Tier 3 reaction should have a deterministic headline
  behaviour (oil *always* ignites) with emergent detail around it.

---

## 7. Decisions for the owner

1. **Simulation resolution:** subcube cells in active regions (recommended), or cubes, or micro
   in small zones only?
2. **Re-bake:** may settled debris and fragments become static voxels again? This reverses the
   locked "no re-bake" decision (DestructionSystemV2 :407). Recommended yes: it is N4.
3. **Water:** keep WaterCore for bodies of water plus the material CA for small spills
   (recommended), or one unified liquid system?
4. **Persistence model** for destructive play (§6), per game or engine-wide.
5. **Where to start:**
   - recommended **Tier 0** (a week-scale batch of real bugs, which also improves the debris feel
     you already have);
   - then a **Tier 2 powder prototype in a one-chunk sandbox** (sand pours, piles and slumps; one
     variable; a control). That prototype answers the resolution and cost questions with numbers,
     not guesses.

## 8. What this analysis did not do
- No runtime measurement. All findings are from code reading.
- Five claims were hand-verified (✔); the rest carry the audits' file:line citations and should
  be re-checked when each item is picked up.
- Noita details come from one making-of video, not from Noita's code.
