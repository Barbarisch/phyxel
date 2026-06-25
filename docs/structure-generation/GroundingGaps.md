# Grounding Gaps Ledger

The single consolidated list of every un-grounded value in the structure-generation spec, so the remaining
grounding work is visible in one place. There are **two kinds**, and the distinction matters:

- **`NEEDS-RESEARCH` (4 real items)** — a value we *tried* to source and couldn't; it needs a specialist source. **These are the genuine gaps.**
- **`to_ground` (~265)** — values deliberately **deferred to the bay/program model** + the brief's status/period at generation time. These are *design choices*, not missing facts (a room's size = its bay count × the style, not a pre-baked constant). Counted in aggregate, not listed individually.

Everything else in the spec is **cited** or **reuse-canon** — and Parts 5–8 + all 45 archetype-sheet numbers passed a grounding-auditor pass.

## NEEDS-RESEARCH — the real gaps (4)

| # | Value | Where | What to consult |
|---|---|---|---|
| 1 | Dovecote **internal boulin/niche** depth × width | `dovecote.md`, `dovecote_interior.md` | Historic England pigeon-house survey; the Dovecote Study Group; SPAB advice notes |
| 2 | **Printing-house** press-room footprint | `printing_house.md` | a print-shop archaeological / architectural survey (workflow is documented; dimensions aren't) |
| 3 | **Foundry** hall size + bloomery-vs-blast-furnace cutoff | `foundry.md` | Agricola *De Re Metallica*; Blanchard, *Mining, Metallurgy & Minting*; furnace archaeology |
| 4 | **Cooper/carpenter** workshop floor for long timber | `cooper_carpenter.md`, `workshop.md` | a surveyed building-yard / cooperage |

All four are honestly flagged in-place; **none are faked into a number.**

## to_ground — deferred to the model (~265)

These are **not** missing facts. They resolve at generation time from:
- the **bay model** (`room_program.json`) × the **style** (`structure_styles.json`) → room sizes;
- the **brief** (status / period / region) → material + scale;
- existing **grounded canons** (`object_dimensions.json` fixtures, Part 5 clearances).

Examples: most room dimensions, inter-building spacing, counter/shelf sizes, pen/yard footprints. **Pre-baking
them as constants would be the *wrong* move** — it's exactly what made v1 produce one-size-fits-all rooms. They're
flagged so the generator knows to *derive*, not assume.

## Resolved on this track (started as gaps, now cited)

- Loom **warp = 24 yd** (assize broadcloth, 1196); **mine adit ~2.29 × 1.14 m** (Agricola); **shop-unit ~2–2.5 m frontage** (Chester selds); **Byczyna gate** single-sourced (medievalheritage.eu).
- Audit corrections applied: **Cloaca Maxima** (height vs width axis), **Great Coxwell** (internal vs external), **Red Lion 1567** (vs The Theatre 1576), **keep walls 2–4 m** (was 1.5 m).

## Migrated to JSON canon (grounded only)

- `object_dimensions.json` — anvil/forge 0.80 m, loom, tan-pit 2×1 m, oven door-ratio, staddle-stone 0.6 m.
- `structure_styles.json` — `stone_keep` (walls 3.0 m).
- `vernacular_materials.json` — region → material map (the build-with-local principle).

*No `to_ground` value was fabricated into the JSON; only cited figures migrated.*
