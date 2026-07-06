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
| 5 | **Bellows** period length/width/loft | `object_dimensions.json` (`bellows`), `blacksmith.md` | Smith, *The Blacksmith's Craft* (1956); Horne, *The Artist-Blacksmith* (2002) — open craft sources give 4 ft; shipped 1.5 m is INFERRED within the 4–6 ft range |
| 6 | **Firebox opening area** (drives the flue size) | `14_place_chimney.md` | Neufert *Architects' Data* (fireplace opening tables); a medieval hall/tavern hearth survey — the 0.44 m² used for the 1/10 flue rule is currently UNSOURCED |
| 7 | **Chimney stack wall thickness** | `14_place_chimney.md` (engine = 1 micro / 0.11 m) | Brunskill *Vernacular Architecture of Britain* (English medieval ≥215 mm brick / ≥300 mm rubble) — the 1-micro masonry wall is thin/unsourced |
| 8 | **Opening-frame stock sizes** (finish_forge P1, 2026-07-05) | `StructureRealizer.cpp` framing pass | Jamb 1 micro (111 mm) vs real frame stock 38–50 mm and lintel 2 micro (222 mm) vs real timber lintels 150–225 mm are voxel-legibility choices on the 1/9-m grid; the GROUNDED part: resulting door clear width 7 micro = 778 mm matches real clear openings 762–813 mm. Sill projection 1 micro (111 mm) vs real 25–75 mm: stylized. Refine against Hewett, *English Historic Carpentry*, if a finer grid ever exists |

All eight are honestly flagged in-place; **none are faked into a number.**

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
- **Smithy footprint** — was `to_ground`; now anchored to an **EXCAVATED smithy: Jamestown 16×20 ft = 4.9×6.1 m** (Historic Jamestowne) for the forge workspace; 2-bay (forge + storefront) is the disclosed design decision. Anvil/forge work-top 0.80 m + anvil 0.60 m + firepot 0.23×0.33 m + quench=barrel cited; **bellows 1.5 m is INFERRED** (open source = 4 ft) → NEEDS-RESEARCH. *(An earlier draft mis-cited the Anderson shop, Colonial Williamsburg — retracted; the text was not on the cited page.)*
- Audit corrections applied: **Cloaca Maxima** (height vs width axis), **Great Coxwell** (internal vs external), **Red Lion 1567** (vs The Theatre 1576), **keep walls 2–4 m** (was 1.5 m).

## Migrated to JSON canon (grounded only)

- `object_dimensions.json` — anvil/forge 0.80 m, loom, tan-pit 2×1 m, oven door-ratio, staddle-stone 0.6 m.
- `structure_styles.json` — `stone_keep` (walls 3.0 m).
- `vernacular_materials.json` — region → material map (the build-with-local principle).

*No `to_ground` value was fabricated into the JSON; only cited figures migrated.*
