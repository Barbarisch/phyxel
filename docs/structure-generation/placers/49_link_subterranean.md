# 49 · link_subterranean

> Tier: Settlement. Part-1 status: **M** (deferred tier). Schema: [`README.md`](README.md).

## Job
**Stub the connections** for the subterranean tier — sewer routes, cellar/crypt/dungeon link points — and hand
off to placers #50–57.

## Reads
- The street net (#39 — sewers run under streets); cellars (#35) + crypts (#32 / Part 8); the subterranean tier (#50–57).

## Emits
- Connection **stubs**: where sewers will run (under the streets, gravity to the river outfall), where cellars/crypts/dungeons link — handed to the subterranean tier.

## Algorithm
1. Mark the **sewer routes** (under the high streets, gravity to the river outfall).
2. Mark the **cellar/crypt/dungeon link points**.
3. Hand off to #50–57 (which need the excavation/connectivity engine gaps).

## Satisfies (checks)
BB3 (connectivity graph), the subterranean handoff.

## Engine capability needed
- Handoff/marking — ✅; the actual subterranean build = #50–57 — ❌ (excavation + connectivity gaps).

## Failure modes
- Sewers not following the streets/gravity; orphaned underground stubs.

## Function testers
- **F1** Sewer routes marked under the streets to a river outfall.
- **F2** Cellar/crypt/dungeon link points marked.
- **F3** Handed to the subterranean tier (not built here).

## Grounding
- REUSE Part 8 (sewer gravity/outfall); the **build is BLOCKED** on excavation + connectivity.

## Open questions
- Whether sewers exist at all for the brief's period (medieval = cesspits; BB9 anachronism flag).
