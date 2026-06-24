# Warehouse / Storehouse — Archetype Data Sheet

> Status: **DRAFT**. Schema: [`README.md`](README.md).

## 1. Identity
- **id:** `warehouse`
- **function:** bulk storage of trade goods
- **aka:** storehouse, staple, (vaulted) undercroft, granary *(grain → see `granary`)*
- **group:** Civic & institutions (Part 6)
- **extends:** a large clear-span store; a vaulted undercroft for valuables (reuse Part 5)
- **genre/period:** real (merchant storehouses, vaulted undercrofts).

## 2. Essence
A **large clear store for goods**, with **cart-width loading** and (dockside/upper floors) a **hoist/crane**;
valuable goods in a secure **vaulted undercroft**. Defining quality: volume + access for bulk + security/dryness.

## 3. Threat model / failure modes
- **Theft** — a secure store (vaulted undercroft, lockable).
- **Fire** + **damp** — dry storage.
- **Load** — a strong floor for stacked goods.

## 4. Access tiers / zoning
- Loading bay / **cart-width doors** → storage floor(s) → a secure vaulted undercroft (valuables); a tally/counting office.

## 5. Required spaces (program)
| Space | Purpose | Required fixtures | Size |
|---|---|---|---|
| storage_floor | bulk goods | racks/stacking, a strong floor | clear span; `to_ground` |
| loading bay | cart access | **cart-width doors** | cart-width |
| hoist / crane | upper-floor / dockside loading | a sack hoist / lucam / crane | upper/water |
| secure undercroft | valuables | a **vaulted** lockable store | REUSE Part 5 undercroft |
| tally office | count goods | a desk, ledgers | `to_ground` |

## 6. Adjacency & circulation rules
1. **Cart-width loading doors** at ground (or a dockside quay face).
2. A **hoist** serves upper floors (a lucam/jutty); a **crane** on a dockside warehouse.
3. The **vaulted undercroft** (valuables) is lockable + dry.
4. A tally/counting point controls goods in/out.

## 7. Construction & materials
- Stone or heavy timber; a **strong floor**; a vaulted undercroft; few/barred windows (security).
- WANTED: cart doors, hoist/lucam, crane (dockside).

## 8. Signature / legibility
A large, plain, **few-windowed** mass with **big cart doors** + a **hoist beam (lucam)** under the gable;
(dockside) a crane.

## 9. Status / period / setting scaling
- **Down:** a merchant's vaulted undercroft beneath a `townhouse`.
- **Up:** a great staple/warehouse (multi-floor + crane) — **dockside variant is BLOCKED on water**.

## 10. Function testers
- **F1** A large clear storage volume.
- **F2** Cart-width loading doors.
- **F3** A hoist for upper floors (or a dockside crane).
- **F4** A dry, secure store (vaulted undercroft for valuables).
- **F5** A tally/counting point.

## 11. Fixtures & assets needed (→ backlog)
Cart doors, sack hoist / lucam, dockside crane, racks, vault door. →
[`WantedAssetsBacklog.md`](../WantedAssetsBacklog.md).

## 12. Grounding ledger
| Claim | Provenance |
|---|---|
| vaulted undercroft for valuables | REUSE-CANON — Part 5 undercroft (medieval undercroft cited there) |
| clear-span store + cart loading + hoist | reasoned/standard merchant storehouse |
| **span / sizes** | `to_ground` |
| dockside crane variant | BLOCKED — water engine feature |

## 13. Open questions / unknowns
- Clear-span limits for a medieval timber warehouse floor — `to_ground`.
