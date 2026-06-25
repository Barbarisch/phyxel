# 52 · place_crypt / place_catacomb

> Tier: Subterranean. Part-1 status: **M**. Schema: [`README.md`](README.md).

## Job
Build **burial chambers + loculi niches** under churches/cemeteries — a crypt under the chancel, catacomb
galleries, multi-level if deep.

## Reads
- The church/cathedral (#45/#46) + graveyard (#32); Part 8 catacomb/loculus dims.

## Emits
- A **crypt** (under the chancel) + **catacomb galleries** (loculi niches in tiers), stairs down, multiple levels if deep.

## Algorithm
1. Excavate (via #50) under the chancel/cemetery.
2. Build galleries (walk-upright) lined with **loculi** (body-sized niches).
3. Stairs down from the church/churchyard; multi-level for a large catacomb.

## Satisfies (checks)
BB5 (period/faith-correct burial — crypt under the chancel, body-sized loculi, oriented), BB2/BB3 (crawlable + connected).

## Engine capability needed
- Gallery/niche carve — ❌ (depends on #50).

## Failure modes
- A crypt not under the chancel; wrong-sized loculi; sealed/unreachable.

## Function testers
- **F1** A crypt under the chancel.
- **F2** Catacomb galleries (~2.5 × 1.0 m) lined with loculi (0.4–0.6 × 1.2–1.5 m).
- **F3** Reachable by stairs; multi-level if deep.
- **F4** Oriented per the rite.

## Grounding
- Catacomb gallery **2.5 × 1.0 m**, loculus **0.4–0.6 × 1.2–1.5 m**, depth **3–25 m** — Part 8 (cited).

## Open questions
- Charnel/ossuary (disarticulated bones) vs loculi (whole bodies) by period/practice.
