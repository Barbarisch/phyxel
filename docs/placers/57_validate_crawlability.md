# 57 · validate_crawlability

> Tier: Subterranean. Part-1 status: **M**. Schema: [`README.md`](README.md). A **gate**, not a builder.

## Job
Prove the subterranean network is **traversable + playable** — widths, headroom, reachability, encounter/light
spacing.

## Reads
- The whole subterranean network (#50–56); the crawlability canon (Part 8 clearances).

## Emits
- A **validation report**: pass/fail on traversability; flags un-crawlable segments, orphans, dead solids.

## Algorithm
1. Walk the network.
2. Check every passage ≥ the walk/crawl clearance (or labelled crawl).
3. Every chamber reachable; encounter/loot/light spacing sensible; secret doors discoverable; dead-ends purposeful.

## Satisfies (checks)
BB2 (crawlable), BB3 (connected), BB8 (playability) — **this is the gate**.

## Engine capability needed
- Graph traversal/validation — ⚠️.

## Failure modes
- An un-crawlable pinch (below clearance, unlabelled); an orphan chamber; a zero-clearance dead solid.

## Function testers
- **F1** Every passage ≥ clearance (or labelled crawl).
- **F2** Every chamber reachable.
- **F3** Encounter/light spacing sensible; secret doors discoverable.
- **F4** It reads as a playable adventure site.

## Grounding
- Clearances — Part 8 (walk **2.032 × 0.914 m**, crawl 0.6–1.0 m; cited).

## Open questions
- Encounter/loot spacing as a tunable (a "dungeon density" param).
