# 29 · place_yard_props

> Tier: Parcel. Part-1 status: **M**. Schema: [`README.md`](README.md).

## Job
Drop the **yard props** — well, trough, woodpile, cart, beehive, midden, mounting block — on the ground at
sensible, functional spots.

## Reads
- The yard zones (#21); the dwelling + outbuildings (a well near the kitchen; a woodpile by the hearth; a midden downwind).

## Emits
- Yard props placed on the ground: well / well-house, water trough, woodpile / log store, cart, beehive (skep), **midden**, mounting block, dung heap.

## Algorithm
1. Place each at its functional spot: **well** central / near the kitchen and **well away from the privy + midden**; **woodpile** against the hearth wall; **midden** downwind + away from the well; cart in the yard; beehives in the garden; mounting block by the door/gate.
2. All **on the ground** (not floating); clear of paths.

## Satisfies (checks)
P (yard props), M (lived-in), Z3 (well separated from midden/privy — contamination), AA (a water source present).

## Engine capability needed
- Prop spawn — ✅; some templates (well, cart, skep) — ⚠️ MISSING → backlog §3.

## Failure modes
- A well next to the midden/privy (contamination — Z3).
- Floating props; props blocking the path.
- An empty, lifeless yard (M).

## Function testers
- **F1** A well / water source sited **away from the privy + midden**.
- **F2** A woodpile by the hearth wall; a midden downwind + away from the well.
- **F3** Props on the ground, clear of paths.
- **F4** The yard reads as **used** (not empty).

## Grounding
- Well-vs-midden/privy separation — Z3 / sanitation (contamination); placement — logical/functional.
- Prop sizes — `to_ground`; templates → backlog.

## Open questions
- Which props by archetype (a smithy yard ≠ a manor forecourt ≠ a tavern stable yard).
