# 48 · dress_street_life

> Tier: Settlement. Part-1 status: **M**. Schema: [`README.md`](README.md).

## Job
Dress the streets with the **props of occupation** — market stalls, woodpiles, laundry, refuse, carts, animals —
scaled by district character.

## Reads
- The streets/market/plots (#39/#43/#45); districts (#41 — a rich quarter vs a slum has different street life); occupancy.

## Emits
- Market **stalls/awnings** (the square); woodpiles + laundry lines + refuse + carts + tethered animals + middens along the streets, scaled by district.

## Algorithm
1. Dress the market with stalls/awnings.
2. Scatter street life (laundry, woodpiles, refuse, carts, animals, wares) **by district** — a slum has more refuse/encroachment, a rich quarter is cleaner.
3. On the ground, not blocking.

## Satisfies (checks)
M (lived-in city), W (district character via street life), AA (a functioning market), BB (slum vs rich street life).

## Engine capability needed
- Prop spawn — ✅; **stall/cart/prop templates** — ⚠️ MISSING (backlog §3).

## Failure modes
- An empty, sterile city (M); identical street life in rich + poor quarters (BB).

## Function testers
- **F1** Market stalls in the square.
- **F2** Street props scaled by district (slum vs rich).
- **F3** On the ground, not blocking.
- **F4** The city reads as inhabited.

## Grounding
- Qualitative; props → backlog §3.

## Open questions
- Market-day vs ordinary-day dressing (a populated market vs an empty square); ties to the day/time fields (#33).
