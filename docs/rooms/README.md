# Room Data Sheets

Per-**room** specifications — the interior layer. [`StructureGenerationPlacers.md`](../StructureGenerationPlacers.md)
Part 3 holds the one-line "what makes it that room" programs; the files here are the **depth** — enough that
`place_fixtures` (#17), `place_furniture` (#16), and the archetype **function-testers** can call them
deterministically. The building layer is `docs/archetypes/`; this is the room layer inside it.

## The room-sheet schema (9 sections)

1. **Identity** — id, purpose, which archetypes use it.
2. **Function** — the defining activity + access tier.
3. **Required fixtures** — the **function-defining** ones (→ `place_fixtures` #17). *Without these it isn't this room.*
4. **Typical fixtures** — comfort/believability (→ `place_furniture` #16), scaled by status.
5. **Service** — the hookups (vent / water / light / drainage / fire-safety).
6. **Adjacency** — where it sits relative to other rooms (its access tier).
7. **Dimensions** — size / clearance (cite / reuse-canon / `to_ground`).
8. **Function testers** — concrete pass/fail (does it work as this room?).
9. **Grounding ledger.**

Rooms reuse the bay model (`room_program.json`), furniture dims (`object_dimensions.json`), and the Part 5
clearances (ceiling 2.134 m, door 2.03 m, etc.). No number stands un-grounded.

## Index (~38 rooms, by category)

**Dwelling (9)** — *this batch*
| room | sheet |
|---|---|
| hall / living | [hall_living](hall_living.md) |
| kitchen | [kitchen](kitchen.md) |
| bedchamber | [bedchamber](bedchamber.md) |
| solar | [solar](solar.md) |
| privy / garderobe | [privy](privy.md) |
| bathing | [bathing](bathing.md) |
| pantry / buttery / larder | [pantry_buttery_larder](pantry_buttery_larder.md) |
| cellar / undercroft | [cellar](cellar.md) |
| servants' quarters | [servants_quarters](servants_quarters.md) |

**Trade & work (8)** — TODO: forge floor, workshop, weaver's, bakehouse, brewery, mill stone-floor, tavern common room, shop/storefront
**Agriculture (6)** — TODO: byre, stable, barn interior, granary interior, dovecote interior, pigsty
**Faith (6)** — TODO: nave, chancel, sacristy, refectory, dorter, scriptorium
**Defensive (5)** — TODO: great hall, guardroom, armory, cell, gatehouse chamber
**Finance / secure (4)** — TODO: banking hall, counting room, vault, ledger archive

*(9 of ~38 specced. Add a row when a sheet lands.)*
