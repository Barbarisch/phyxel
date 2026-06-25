# 02 · prepare_pad

> Tier: Site & shell. Part-1 status: **M**. Schema: [`README.md`](README.md).

## Job
Turn bumpy terrain into a **level build pad** — cut the high side, fill the low side (or step/terrace on a
slope), and retain the edges — so the foundation has a flat datum.

## Reads
- The **SiteReport** from analyze_site (#1): per-cell height, slope, water.
- `AssemblyPlan`: footprint, the chosen pad level (or "derive").
- Style: retaining-wall material/thickness (reuse the foundation/keep canon).

## Emits
- **Terrain edits**: cut voxels above the pad level, fill voxels below it (to the pad datum), across the footprint (+ a working skirt).
- **Retaining edges** where cut/fill meets original grade (a low retaining wall / batter).
- On a steep site: **stepped/terraced** pad levels + the steps between them.
- Updates the SiteReport's ground to the new flat datum.

## Algorithm
1. Choose the pad level = the **median** ground height (minimizes cut+fill) unless the brief pins it.
2. For each footprint cell: cut everything above the level; fill (with compacted earth/the substructure material) up to it.
3. Where the cut face or fill toe meets original grade, build a **retaining edge** (height = the local cut/fill depth).
4. If slope > the single-pad threshold, split into **terraces** (each a sub-pad) linked by steps; re-run per terrace.
5. Hand the flat datum to place_foundation (#3).

## Satisfies (checks)
D (the foundation sits on level bearing), L (the structure isn't perched/floating), M (reads as grounded), and the "foundation not one-cube perched" complaint.

## Engine capability needed
- **Terrain excavation + fill** — ❌ **MISSING** (carve voxels out of chunk terrain and add compacted fill; the same core gap as basements/subterranean). This placer is the first to *force* that capability.
- Retaining-wall emit — ✅ (reuse the wall paint path).
- Physics rebuild after terrain edit — ⚠️ (`buildAllChunkPhysics()` must re-run on the edited chunks).

## Failure modes
- Cut/fill without rebuilding occupancy → characters fall through / float.
- Filling over water without drainage → flag (a pad in a marsh).
- Over-deep cut → becomes a basement decision (hand to excavate_basement #34).

## Function testers
- **F1** The footprint is flat to a single datum (or stepped terraces, each flat).
- **F2** Cut + fill balanced near the median (no gratuitous earthmoving).
- **F3** Retaining edges where the pad meets original grade.
- **F4** Occupancy/physics rebuilt on edited chunks.
- **F5** The structure sits *in/on* the ground, not perched on a 1-cube plinth.

## Grounding
- **Max slope before terracing** + **max single cut/fill depth** — `to_ground` (vernacular practice: build into the hill, terrace rather than tower the foundation).
- Retaining-wall thickness — REUSE the foundation/retaining canon.

## Open questions
- Cut spoil reuse as fill vs disposal — track a spoil budget?
- Terrace step height tie-in with place_entry (#20) + place_stairs (#12).
