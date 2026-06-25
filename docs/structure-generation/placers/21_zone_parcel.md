# 21 · zone_parcel

> Tier: Parcel. Part-1 status: **M**. Schema: [`README.md`](README.md). First of the parcel/landscape tier.

## Job
Lay out the lot — place the building on the plot and zone it: **front yard** (public / approach / ornamental)
vs **back yard** (private / utility / work), with context-correct setbacks.

## Reads
- `AssemblyPlan` footprint; brief parcel type (rural croft / **urban burgage** / manor close); the approach edge (#1); Part 7 burgage frontage for urban.

## Emits
- Parcel **zones** (front/public, back/private-utility, side), setbacks, the building's placement on the plot, and zone tags the other parcel placers read.

## Algorithm
1. Place the building per context: **street-fronting** for a burgage (no front setback, deep back yard); a front approach + a working back yard for rural; within the walls for a manor close.
2. Zone **front** (approach / public / ornamental garden) vs **back** (privy, midden, kitchen garden, livestock, work).
3. Apply setbacks by context (dense urban = none; rural = generous).

## Satisfies (checks)
L (parcel layout — front vs back), P (parcel/landscape), Y (urban plots/density — burgage, not a freestanding cottage in a city).

## Engine capability needed
- Parcel layout logic — ⚠️ (pure logic, not written).

## Failure modes
- A freestanding-cottage-with-yard dropped into a dense city (should be a party-wall burgage — Y1).
- Noxious uses (privy/midden/sty) zoned to the front.

## Function testers
- **F1** A front (public/approach) zone **and** a back (private/utility) zone.
- **F2** Building placed per context (street-fronting burgage vs set-back rural vs walled close).
- **F3** Noxious uses (privy / midden / sty) in the **back**, downwind.

## Grounding
- Burgage frontage/depth — REUSE Part 7 (cited); rural plot proportions — `to_ground`.

## Open questions
- Corner-plot handling (two street frontages); shared back lanes (Part 7).
