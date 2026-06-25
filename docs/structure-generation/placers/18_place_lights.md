# 18 · place_lights

> Tier: Interior. Part-1 status: **M**. Schema: [`README.md`](README.md).

## Job
Light each room — **daylight accounting** from the windows + **artificial light** props (with engine point
lights) to the level the room's use needs.

## Reads
- Rooms + windows (#10) for daylight (area + orientation); room use (a workshop needs more than a store); period (candle/rushlight/lamp/torch/brazier; magical `glow` for fantasy); status.

## Emits
- A light **prop** per room (wall sconce, table candle, hung **chandelier**, torch, brazier) + a registered **engine point light**; daylight from windows accounted so artificial light only fills the deficit.

## Algorithm
1. Estimate daylight per room from window area + orientation.
2. Compare to the use's need (workshop/scriptorium high; hall medium; store low) → the artificial-light deficit.
3. Place period-correct props to fill it (sconce on a wall, candle on a table, chandelier hung in a hall) + register point lights.
4. Fantasy → the `glow` material / magical light per the world bible.

## Satisfies (checks)
K (lighting per room), B (daylight), the workshop "good light" testers (weaver/scriptorium), M (mood).

## Engine capability needed
- Point-light add — ✅ (`add_point_light`).
- `glow` emissive material — ✅.
- Light-prop templates — ⚠️ (a candle is fine; **chandelier MISSING** → backlog §3).

## Failure modes
- A dark, unlit interior (no daylight estimate, no props).
- A chandelier **faked** because the asset is missing (flag, don't fake).
- Anachronistic lighting (a lamp where a rushlight belongs).

## Function testers
- **F1** Every occupied room reaches adequate light (daylight + artificial).
- **F2** Light props are period + status correct.
- **F3** Engine point lights registered (it actually lights at runtime).
- **F4** A missing fixture (chandelier) is flagged, not faked.

## Grounding
- Light level by use — `to_ground`; period light sources (rushlight/candle/lamp/torch) — A (period).

## Open questions
- Day/night + the DayNightCycle interaction (artificial light matters more at night).
