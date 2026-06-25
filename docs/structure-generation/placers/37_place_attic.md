# 37 · place_attic

> Tier: Vertical. Part-1 status: **M** (the roof void is incidental, sealed + dark). Schema: [`README.md`](README.md). See Part 5.

## Job
Make the **roof volume** a usable story — a usable-area mask from the pitch (headroom ≥ 1.5 m), knee walls, a
sloped ceiling, dormers/gable lights, and hatch/stair access.

## Reads
- The roof shell (#13) + its pitch; the top story's ceiling (= the attic floor); Part 5 attic canon.

## Emits
- The attic **floor** (= the top ceiling joists); a **usable-area mask** (only where headroom ≥ 1.5 m under the pitch); **knee walls** closing the eaves; a sloped ceiling on the rafters; **dormers / gable windows** for light; **access** (hatch / ladder / stair via #12).

## Algorithm
1. Take the roof void; compute the **headroom mask** — the strip where the pitch gives ≥ 1.5 m.
2. That mask = the usable attic room (smaller than the footprint; eaves excluded).
3. Build **knee walls** at the mask edge; floor the usable area.
4. Punch **dormers / gable windows** for light; cut a **hatch/stair** (#12).

## Satisfies (checks)
V7 (usable-area mask ≥ 1.5 m), V8 (access + light — not sealed/dark), V9 (roof structure suits an occupiable attic).

## Engine capability needed
- Roof void exists (#13) — ✅; headroom-mask logic — ⚠️; **dormer (roof penetration)** — ❌ (the dormer gap); hatch/stair via #12.

## Failure modes
- Flooring the **whole footprint** (ignoring the mask — eaves unusable).
- A **sealed, dark** attic (no access/light — the current incidental void).
- No knee walls.

## Function testers
- **F1** Usable floor **only where headroom ≥ 1.5 m** (the room is smaller than the footprint).
- **F2** Knee walls close the eaves.
- **F3** Access (hatch / ladder / stair).
- **F4** Light (dormer / gable window).
- **F5** Not sealed/dark.

## Grounding
- Usable headroom **≥ 1.5 m**; standing **2.0–2.2 m** — REUSE Part 5 (RICS / Approved Doc K, cited).
- Knee-wall height — derived from the mask (Part 5).

## Open questions
- Dormer vs gable-light vs roof-light by period/status (dormers are a roof-penetration feature, currently missing).
