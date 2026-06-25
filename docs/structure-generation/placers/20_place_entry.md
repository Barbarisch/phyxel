# 20 · place_entry

> Tier: Interior (threshold). Part-1 status: **P** (step logic). Schema: [`README.md`](README.md).

## Job
Make the building **enterable** — steps / a threshold from grade up to the main door sill, a stoop/porch where
status warrants — bridging the foundation/pad height to the door.

## Reads
- The main door (#9) + its sill height; the pad/grade datum (SiteReport, #1/#2); the approach edge.
- Status (a stoop/porch/portico by wealth).

## Emits
- **Steps** (or a ramp/threshold) from grade up to the door sill; a **stoop/porch** where appropriate; a handoff point to place_path (#24) at the property line.

## Algorithm
1. Compute the **rise** from grade to the door sill (the foundation/plinth lift).
2. If the rise > a threshold, build **steps** (grounded rise/run); else a simple sill/threshold.
3. Add a stoop/porch (status) sheltering the door.
4. Land the steps on the **approach edge** and hand off to the path (#24).

## Satisfies (checks)
G (access from grade — you can actually get in), L (entry/approach), and the **"perched foundation — no way in"** complaint.

## Engine capability needed
- Step voxels — ✅.
- **Grade-to-sill computation** — ⚠️ (needs the SiteReport datum + the foundation lift).

## Failure modes
- A door over a raised sill with **no steps** → you can't enter (the perched-building bug).
- Steps too steep / floating; not landing on the approach.

## Function testers
- **F1** A continuous **walkable route from grade to the door sill** (no impassable lip).
- **F2** Steps grounded (rise/run); a threshold at the door.
- **F3** A stoop/porch where status warrants.
- **F4** The entry lands on the approach edge + connects to the path (#24).

## Grounding
- Step rise/run — REUSE Part 5 stair canon (or a gentler external stair); threshold height — `to_ground`.

## Open questions
- Mounting block / ramp variants; how a terraced pad (prepare_pad #2) changes the entry rise.
