# 54 · place_mine

> Tier: Subterranean. Part-1 status: **M**. Schema: [`README.md`](README.md).

## Job
Drive a **mine** — adit → galleries (following an ore seam) → shafts — timbered, with spoil at the mouth.

## Reads
- An ore seam (terrain/resource); brief (a mine); Part 8 / Agricola adit dims.

## Emits
- An **adit** (horizontal entry) → **galleries** following the seam → **shafts** (vertical); timber supports; spoil heaps at the adit mouth; a headframe/winch at a shaft.

## Algorithm
1. From the adit mouth, drive galleries **along the seam**.
2. Sink shafts; timber the supports.
3. Spoil at the mouth; ventilation.

## Satisfies (checks)
BB7 (mines follow a seam, timbered, spoil at the mouth — not random caverns), BB2 (crawlable).

## Engine capability needed
- Gallery/shaft carve — ❌ (depends on #50); timber supports — ✅.

## Failure modes
- Random caverns (not seam-following); untimbered (collapse); no spoil at the mouth.

## Function testers
- **F1** An adit → galleries (following a seam) → shafts.
- **F2** ~**2.29 m high × ~1.14 m wide** (Agricola); timbered.
- **F3** Spoil at the mouth; ventilated.

## Grounding
- Adit **~2.29 m high × ~1.14 m wide** (Agricola, *De Re Metallica* Bk V — Part 8 cited); shaft (Agricola: 2 fathom × ⅔ fathom × ~13 fathom deep).

## Open questions
- Ore-seam generation (where the seam is) — ties to world-gen geology.
