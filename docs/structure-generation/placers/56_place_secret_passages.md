# 56 · place_secret_passages

> Tier: Subterranean. Part-1 status: **M**. Schema: [`README.md`](README.md).

## Job
Place **hidden doors / tunnels** — between buildings, to an escape route, or to the underground — with trigger
mechanisms.

## Reads
- The structure + the underground (#55); brief (intrigue — manor / castle / thieves' den); the gambling-den bolt-hole, priest-hole, etc.

## Emits
- Concealed doors (behind a bookshelf / panel / fireplace), hidden tunnels (escape route, smuggler's run), trigger mechanisms.

## Algorithm
1. Place concealed doors/tunnels where the brief/archetype wants intrigue (a lord's escape tunnel, a thieves' bolt-hole, a priest-hole).
2. A trigger; connect to the underground (#55) or another building.

## Satisfies (checks)
BB (subterranean), the gambling-den / thieves' / escape testers, BB8 (discoverable — playability).

## Engine capability needed
- Concealed-door mechanism — ⚠️; tunnel — ❌ (depends on #50/#55).

## Failure modes
- A "secret" door that's obvious or non-functional; a tunnel to nowhere.

## Function testers
- **F1** Concealed doors/tunnels where the archetype wants them.
- **F2** A trigger mechanism.
- **F3** **Discoverable** (not impossible, not obvious).
- **F4** Connects somewhere real.

## Grounding
- Qualitative; tunnel dims reuse #50.

## Open questions
- The discoverability tuning (a perception/search check threshold) — gameplay tie-in.
