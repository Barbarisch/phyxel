# 15 · place_trim

> Tier: Closure & roof. Part-1 status: **M**. Schema: [`README.md`](README.md).

## Job
Add the **micro-scale detail** that makes a building read as finished + status-appropriate — quoins, casings,
string courses, exposed framing, cornices, baseboards.

## Reads
- Corners (quoins tagged by #6), openings (casings, from #8), floor lines (string courses), eaves (cornice).
- Brief: status (more + finer trim = higher status), style (timber-frame ↔ masonry → different detail).

## Emits
- **Microcube** detail: quoins (alternating corner stones), door/window **casings/architraves**, **string courses** at floor lines, **exposed studs + braces** (half-timber), **cornice** at the eaves, baseboards.

## Algorithm
1. By status + style, select a trim package (croft = none; townhouse = exposed frame; manor = quoins + casings + cornice + string courses).
2. Place each element at microcube resolution at its location (corners, openings, floor lines, eaves).
3. Respect a **micro budget** (micro detail is instance-expensive) — apply where it reads, not everywhere (N).

## Satisfies (checks)
J (finish + ornament), C (status shown in trim), M (aesthetic coherence), N (micro-detail cost budget).

## Engine capability needed
- Microcube paint — ✅.
- (Cost): subcube/micro instance budget awareness — ⚠️ (apply by status).

## Failure modes
- A palace with no ornament (C); a peasant croft with fancy cornices (A/C).
- Micro detail on every surface → instance blow-up (N).
- Ahistorical profiles (A).

## Function testers
- **F1** Trim scales with status (none → quoins/casings/cornice).
- **F2** Detail placed only where it reads (corners/openings/eaves/floor lines), within the micro budget.
- **F3** Style-correct: exposed frame on timber, dressed quoins on masonry.
- **F4** Period-correct profiles (no anachronistic mouldings).

## Grounding
- Trim is qualitative + status-driven; quoins / string courses / cornices are standard medieval detail.
- Specific profiles — `to_ground`.

## Open questions
- Carved ornament (label stops, corbels, bargeboards) as a higher-status tier — ties to the sculpture/decal backlog.
