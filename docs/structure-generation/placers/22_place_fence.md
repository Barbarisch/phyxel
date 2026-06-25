# 22 · place_fence

> Tier: Parcel. Part-1 status: **M**. Schema: [`README.md`](README.md).

## Job
Run a **fence or hedge** along the lot boundary — the right type for the use — with posts at grounded spacing
and a **gate where the path crosses**.

## Reads
- Parcel boundary (#21); the path crossing (#24); brief (fence type by status/use).

## Emits
- A fence line — **posts + rails + pales/pickets**, a woven **wattle hurdle**, a **cleft pale**, or a **quickset hedge** — + a gate at the path.

## Algorithm
1. Choose the type: low **picket/pale** for a front garden; tall **close-board / hurdle** for stock or privacy; a **hedge** for a field/croft.
2. Set posts at ~6–8 ft spacing; rails ~1 per 24" of height; pales/weave between.
3. Put a **gate** where the path (#24) crosses the boundary.

## Satisfies (checks)
P (boundary fence), L, C (status — a trim front fence vs a rough stock hurdle), A (period-correct form).

## Engine capability needed
- Post/rail/pale voxel — ✅; wattle-hurdle weave — ⚠️; gate — ⚠️.

## Failure modes
- A 6 ft suburban privacy fence around a peasant croft (C/A — anachronistic).
- **Machine-milled pickets** in a medieval brief (A — use a cleft pale / woven hurdle / quickset hedge).

## Function testers
- **F1** A boundary fence/hedge of the right type for the use.
- **F2** Posts at grounded spacing; a gate where the path crosses.
- **F3** Height grounded to the type.
- **F4** **Period-correct form** (hurdle / cleft pale / hedge for medieval — not a milled picket panel).

## Grounding
- picket **0.9–1.2 m** (3–4 ft); privacy **1.8 m** (6 ft); posts **6–8 ft** spacing; ~1 rail / 24" — CITED (modern fence standards, the brief's reference). [Omnicalculator fence](https://www.omnicalculator.com/construction/fence-picket)
- **Medieval forms** (wattle hurdle ~1.5–1.8 m, cleft pale, quickset hedge) — `to_ground` (the calculators are modern; the heights transfer, the construction doesn't).

## Open questions
- Hurdle weave as a distinct asset vs a textured band; hedge as flora (ties to #25).
