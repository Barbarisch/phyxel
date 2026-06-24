# 24 · place_path

> Tier: Parcel. Part-1 status: **M**. Schema: [`README.md`](README.md).

## Job
Lay a graded **path** from the door (entry #20) to the road/gate — surface by status, steps on a slope, routed
around the garden beds.

## Reads
- The entry (#20) + the gate (#22); the road/approach edge (#1); terrain; garden beds (#25) to avoid.

## Emits
- A path (surface: beaten earth / gravel / flag / cobble by status) from the door to the gate/road; steps on a slope; routing around beds/obstacles.

## Algorithm
1. Route **door → gate → road** (shortest sensible line, around beds + obstacles).
2. Surface by status (croft = beaten earth; manor = flag/gravel; town = cobble).
3. Add **steps** where the grade exceeds a comfortable walk.
4. Set width by use (foot vs cart — reuse the Part 7 street-width derivation).

## Satisfies (checks)
G (access — a real route to the door), L (path/approach), P.

## Engine capability needed
- Path-surface paint — ✅; routing + grade/step logic — ⚠️.

## Failure modes
- No path at all (mud to the door).
- A path straight through a garden bed.
- Too steep with no steps.

## Function testers
- **F1** A walkable path **door → gate → road**.
- **F2** Surface per status.
- **F3** Steps where the grade is steep.
- **F4** Routes around beds; width per use (foot/cart).

## Grounding
- Path width — REUSE Part 7 (foot ~1 m / cart ~2.5–3 m, derived); steps — Part 5 stair canon.
- Surface material — `to_ground` by status/region.

## Open questions
- Desire-line (organic) vs formal (axial) path by status; connection into the settlement street net (Part 7).
