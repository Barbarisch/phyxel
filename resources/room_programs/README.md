# Era room-program packs

The structure forge is **period-aware** (`StructureForge.cpp`, "M8 PERIOD AXIS"): a building
program's `period` selects the room programs it may use, and an unknown period **refuses**
rather than substituting another era's house (the no-substitution rule).

- `medieval` is the shipped default and lives in the legacy file `resources/room_program.json`
  (every program there carries `period: "medieval"` implicitly).
- **Every other era is a data pack in THIS directory:** `room_programs/<period>.json`, loaded by
  `RoomProgramRegistry::loadPeriodPack(path, period)` when a build asks for that `period`.
  No code change adds an era.

## Pack shape

Same schema as `resources/room_program.json` (`programs.<typology>` with `width_min/max`,
`bay_length`, `bays`, `proportion_*`, `stories`, `entrance`, `windows`, `rooms[]`, `sources`),
optionally wrapped with a top-level `"period": "<id>"` that overrides the caller's label:

```json
{
  "period": "roman",
  "programs": {
    "insula": { "...": "grounded dimensions with sources, as in room_program.json" }
  }
}
```

## Who passes `period`

- `POST /api/structure/build` (schema v2): `"period": "<id>"` (absent = medieval).
- Settlement builds: `SettlementBuildService` forwards the settlement's `era` as every
  building's `period` (WorldForge v2 plan, M0 row "period passthrough", 2026-10-14). Before
  that row the era never reached a building and every era built medieval houses.

## Adding an era (the D9 falsification in docs/WorldForgeV2Plan.md §5 M5)

1. Add `eras.<id>` to `resources/settlement_program.json` (tiers, typology weights, street data).
2. Add `room_programs/<id>.json` with at least one typology, every dimension sourced
   (grounding-auditor).
3. The realizer's era-specific rules (roof forms, bays, ornament) are code until M5 makes them
   data; see the plan.
