# 23 · place_boundary_wall

> Tier: Parcel. Part-1 status: **M** (v1 `generateWallSegment` primitive only). Schema: [`README.md`](README.md).

## Job
Build a **freestanding boundary / garden / retaining wall** between points — dry-stone, cob, or brick/stone with
a coping — at a stable tapered profile.

## Reads
- The boundary / wall run (#21); style (region → dry-stone vs cob vs brick); height by use.

## Emits
- A freestanding wall built to height with a **tapered base**, **through-stones** (dry-stone), and a **coping**; a gate/opening.

## Algorithm
1. Pick the material by region/style (stone country → dry-stone; cob country → cob; brick where made).
2. Build to height with the grounded **base-width taper** (wider at the bottom, narrowing up).
3. Place **through-stones** at intervals (dry-stone) for stability; cap with a **coping**.
4. Leave a gate/opening where the path crosses.

## Satisfies (checks)
P (boundary wall), D (a tapered base = stable, not a 1-voxel sliver), L.

## Engine capability needed
- Tapered freestanding-wall paint — ⚠️ (the v1 `generateWallSegment` primitive exists; taper + through-stones missing).

## Failure modes
- A vertical 1-cube-wide "dry-stone" wall (no taper → unstable-looking, D).
- The wrong regional material (a brick wall in a stone-country croft, A/J).

## Function testers
- **F1** Wall height per use (garden ≈ 1.4 m, boundary ≈ 1.6 m, retaining = the local cut depth).
- **F2** Base width grounded + **tapered** (~0.75 m base for a ~1 m wall).
- **F3** Through-stones (dry-stone) + a coping.
- **F4** Regional material; a gate/opening where needed.

## Grounding
- Dry-stone freestanding **~1.4 m** (4'6"), boundary **~1.6 m** (5'3"); base **~0.75 m** for ~1 m height, tapering up; through-stones at intervals — CITED ([The Stone Trust](https://thestonetrust.org/tech-specs-for-dry-stone-walls/); [Dry stone (Wikipedia)](https://en.wikipedia.org/wiki/Dry_stone)).
- Cob / brick wall dims — `to_ground`.

## Open questions
- Mortared vs dry by region/status; wall-top coping style.
