# 09 · place_doors

> Tier: Closure & roof. Part-1 status: **M**. Schema: [`README.md`](README.md).

## Job
Hang **door leaves** in the door openings — correct handedness/swing — and register them with `DoorManager` so
they open/close/lock.

## Reads
- Door openings (kind = door/arch) from cut_openings (#8); the room each side (from the portal graph).
- Brief/program: `lockable`, `key`; material by status.

## Emits
- A door **leaf** (a kinematic voxel group), hung on a hinge side, with a clear swing arc.
- A `DoorManager` registration (open/close, lock state, key).

## Algorithm
1. For each door opening, pick the **hinge side** so the leaf swings into the less-trafficked / larger room and **clears furniture + other doors**.
2. Size the leaf to the opening; material/finish by status (plank ↔ panelled ↔ iron-bound).
3. Hang it (kinematic group pivoting on the hinge edge); set lock + key from the program.
4. **Register with `DoorManager`** so gameplay can operate it.

## Satisfies (checks)
H (doors present + correct), G (door clearance + swing doesn't foul), N (DoorManager registration).

## Engine capability needed
- `DoorManager` register / open / close / lock — ✅ (`register_door`, door MCP tools exist).
- Kinematic door leaf voxel group — ✅ (`kinematic_voxel` pipeline).

## Failure modes
- Leaf swings into a wall / another door / furniture → G.
- Wrong handedness (opens against the traffic flow).
- Not registered → a painted-on door that can't open.

## Function testers
- **F1** A leaf in every door opening, sized to it.
- **F2** Hinge side chosen so the swing arc is clear.
- **F3** Registered with `DoorManager` (operable).
- **F4** Lockable doors carry their key reference.

## Grounding
- Door clear height — REUSE Part 5 canon (interior 2.03 m); leaf thickness `to_ground`.

## Open questions
- Double doors / portcullis / drawbridge as door variants (castle) — separate handling or a leaf-count param.
