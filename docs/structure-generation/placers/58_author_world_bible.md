# 58 · author_world_bible

> Tier: Fantasy (see Part 9). Part-1 status: **M**. Schema: [`README.md`](README.md).

## Job
Author / load + **consistency-validate** + persist the **setting canon** — magic, pantheon, races, factions,
era — per world. The source-of-truth for all fantasy grounding.

## Reads
- Brief (setting = fantasy + a world-bible ref); the existing world bible (if any); Part 9.

## Emits
- The persisted **`world_bible`** (magic rules, magical materials, pantheon, races/cultures + their architecture, factions, era), consistency-validated; per-world (like the world recipe).

## Algorithm
1. Author or load the bible.
2. Validate **internal consistency** (no rule contradicts another).
3. **User-approve**; persist per-world (like `world_meta`).

## Satisfies (checks)
CC1 (fantasy values trace to a bible entry), CC2 (internal consistency), the **"engine remembers, not Claude"** rule (Part 4/9).

## Engine capability needed
- **Bible schema + consistency validator + persistence** — ❌ MISSING (Part 9 target); the `glow` material exists.

## Failure modes
- Fantasy values with no bible source (CC1 — invented).
- Contradictory bible rules (CC2).

## Function testers
- **F1** The bible exists + is persisted per-world.
- **F2** Internally consistent (the validator passes).
- **F3** Every fantasy value can trace to it.
- **F4** Reproducible without Claude.

## Grounding
- The bible **is** the grounding source for fantasy (Part 9); the mechanism = the established **story/world-bible** practice (cited, Part 9).

## Open questions
- Shared/global bible (a setting) vs per-world overrides.
