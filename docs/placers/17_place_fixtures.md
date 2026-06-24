# 17 · place_fixtures

> Tier: Interior. Part-1 status: **P** (via the furniture map). Schema: [`README.md`](README.md).

## Job
Place the **function-defining fixtures** — the things that make a room *work* as its type (cooking hearth, forge,
altar, bar, loom, counter, vault door) — at their functionally-correct spot, with their **service** hooked up.

## Reads
- Room purpose + the **archetype data sheet** → the room's **required fixtures** (Part 3 "required" + "service" columns).
- Walls/openings (#6–8) for placement (vented wall, east end, window wall).

## Emits
- Each required fixture (template / voxel group) **+ its service tag**: a hearth/oven/forge tagged for a flue (chimney #14); a fixture needing water/drainage tagged; an altar oriented; a bar on the public/service line; a vault door on the strongroom.

## Algorithm
1. From the room's required-fixtures list, take each function fixture.
2. Place it at its functionally-correct location: cooking hearth/oven on a **vented (chimney) wall**; **forge** on the back wall, **clear of thatch**; **altar** at the **east** end, raised; **bar/counter** on the public↔service boundary; **loom** at the **window** wall; **vault door** on the strongroom.
3. Tag the **service** each needs (flue → #14; water/drain; daylight → #18) so downstream placers satisfy it.
4. Verify the room now passes its **T function test**.

## Satisfies (checks)
**T (the room function testers — REQUIRED fixtures present + serviced)**, K (fixtures), and each archetype's `function_test` (the bank vault, the kitchen cooking station, the church altar).

## Engine capability needed
- Template spawn / voxel-group place — ✅.
- **The fixture templates themselves** — ❌ mostly MISSING (forge, anvil, loom, altar, bar, vault door, oven, counter…) → backlog §3.

## Failure modes
- A "kitchen" with no cooking station, a "church" with no altar (T1 fail — the whole point of the function testers).
- A fixture present but **not serviced** (a hearth with no flue; a forge under thatch; an altar facing west).

## Function testers
- **F1** Every room's **required** fixtures (per Part 3 / the archetype sheet) are present.
- **F2** Each is placed **functionally** (forge back wall, altar east, bar on the boundary, loom at light).
- **F3** Each is **serviced** (flue / water / drainage / light tagged for downstream placers).
- **F4** The room passes its T function test.

## Grounding
- Fixture dims — `object_dimensions.json` (expand for the new fixtures); placement rules — REUSE Part 3 service column + the archetype sheets (cited).

## Open questions
- Boundary with place_furniture (#16): fixtures = function-defining + serviced; furniture = comfort/casegoods. Merge or keep split? (Recommend: fixtures first, furniture fills around them.)
