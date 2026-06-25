# 45 · populate_plots

> Tier: Settlement. Part-1 status: **M**. Schema: [`README.md`](README.md).
> **Invokes the entire single-building pipeline (#1–37) per lot.**

## Job
For each plot, pick a Part 6 archetype by **district + status + frontage**, and **run the building pipeline
(#1–37)** to build it.

## Reads
- The plots (#40) + district/wealth/trade (#41) + frontage; the Part 6 archetype library; the brief→build pipeline (Part 10).

## Emits
- A built structure per plot — the archetype chosen by (district trade, wealth tier, frontage), realized by running placers #1–37.

## Algorithm
1. For each plot, derive the archetype via the **Part 10 decision tables** (district trade + wealth tier + frontage → townhouse / shop / tavern / church / …).
2. Run the building pipeline (#1–37) on the plot.
3. Share **party walls** with neighbours in the core; allow **accretion** (mixed ages).

## Satisfies (checks)
Y (plots built as the right archetype), W (archetype fidelity per plot), Z (district character realized), AA7 (a believable trade mix), CC (accretion/mixed age).

## Engine capability needed
- **Invoke the building pipeline per plot** — depends on #1–37 (mostly spec) + the Part 10 derivation engine — ⚠️.

## Failure modes
- Every plot the same building (no district variety — Z1).
- A cathedral on a 5 m burgage plot (frontage/archetype mismatch).
- One-of-everything with no believable spread (AA7).

## Function testers
- **F1** Each plot built as a district + status + frontage-appropriate archetype.
- **F2** Party walls in the core.
- **F3** A believable trade mix (not one-of-everything).
- **F4** Accretion (mixed ages).
- **F5** Each district reads as its character.

## Grounding
- Archetype selection — REUSE Part 10 decision tables + Part 6; frontage → archetype — `to_ground`.

## Open questions
- Plot-to-archetype fit when a big archetype (church/guildhall) needs several merged plots.
