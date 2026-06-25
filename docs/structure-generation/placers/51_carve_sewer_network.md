# 51 · carve_sewer_network

> Tier: Subterranean. Part-1 status: **M**. Schema: [`README.md`](README.md).

## Job
Carve **vaulted drains under the streets** — fed by garderobe chutes + cesspits, falling by gravity to a river
outfall, with surface gratings.

## Reads
- The street routes + outfall (#49/#39); the river; Part 8 Cloaca dims; the BB9 anachronism flag.

## Emits
- Vaulted sewer tunnels under the streets (barrel-vault), inlets from garderobe chutes + cesspits, a **fall to the river outfall**, surface gratings/manholes.

## Algorithm
1. Route **under the streets**, following **gravity to the river outfall**.
2. Vaulted section: Cloaca-sized for a walkable main, smaller for a branch drain.
3. Connect chutes/cesspits; gratings to the surface.

## Satisfies (checks)
BB4 (gravity to outfall, under streets, chutes, gratings), BB9 (walkable sewer = Roman/Victorian **anachronism** — flag for medieval), BB2 (crawlable).

## Engine capability needed
- Vault carve — ❌ (depends on excavate_subterrane #50); water flow — ⚠️.

## Failure modes
- A sewer running uphill (no gravity); a free-floating maze; a walkable sewer in a strict-medieval brief **unflagged** (BB9).

## Function testers
- **F1** Sewers run **under the streets**, gravity to a **river outfall**.
- **F2** Fed by garderobe chutes + cesspits; surface gratings.
- **F3** Walkable mains ≥ clearance, OR small drains (labelled).
- **F4** Anachronism flagged for a medieval brief.

## Grounding
- Cloaca **2.7–3.3 m high × 2.1–4.5 m wide** (walkable, Roman — Part 8 cited); walk clearance **2.032 × 0.914** (Part 8).
- Medieval = cesspits + open kennels (BB9 anachronism).

## Open questions
- Open street-gutter (medieval norm) vs covered sewer (Roman/fantasy) by brief.
