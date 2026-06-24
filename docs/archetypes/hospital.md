# Hospital / Hospice — Archetype Data Sheet

> Status: **DRAFT**. Schema: [`README.md`](README.md).

## 1. Identity
- **id:** `hospital` / `hospice`
- **function:** care of the sick, poor, aged, and travellers (body + soul)
- **aka:** infirmary, spital, Maison Dieu, almshouse *(variant)*
- **group:** Civic & institutions (Part 6) — usually a **religious foundation**
- **extends:** the monastic infirmary plan (an aisled hall + a chapel)
- **genre/period:** real (medieval hospitals/almshouses).

## 2. Essence
An **aisled infirmary hall with beds along the aisles, opening onto a chapel at the east end** — care of body
*and* soul together. Defining quality: the bed-hall + the chapel **in full view of every bed**.

## 3. Threat model / failure modes
- **Disease / contagion** — segregation (men/women), ventilation, the chapel sightline for the dying.
- **Charity / care** — a religious foundation with a warden + staff.

## 4. Access tiers / zoning
- The **infirmary hall** (beds in aisles) → **chapel** (east, visible from every bed) → dispensary + warden's lodging.
- Men and women **segregated** (separate halls + chapels).

## 5. Required spaces (program)
| Space | Purpose | Required fixtures | Size |
|---|---|---|---|
| infirmary_hall | house the sick | **beds ranged along the aisles**, open to the chapel | aisled, ~**4 bays / 20 m aisles / ~16 beds** (Great Hospital); bed count by settlement (below) |
| chapel | care of soul | an **altar at the east end**, in full view | east end |
| dispensary | medicines | shelving, prep | `to_ground` |
| warden/staff lodging | run it | dwelling fixtures | near entrance |

**Bed count by settlement:** village/small town **10–25**; medium city **30–100**; great city (Paris/Lyon) **100+**.

## 6. Adjacency & circulation rules
1. The **chapel is at the east end, in FULL VIEW** of the beds (care of soul).
2. **Beds ranged along the aisles** of the hall.
3. **Men and women segregated** — separate halls (or a partition) + separate chapels.
4. Dispensary + warden near the entrance.

## 7. Construction & materials
- An aisled hall (arcade) + a chapel; large windows (light/air); masonry or timber.
- WANTED: beds, altar, stained glass (chapel), dispensary shelving.

## 8. Signature / legibility
A long **aisled hall opening onto a chapel** — like a church with beds; a religious foundation's plainness.

## 9. Status / period / setting scaling
- **Down:** a small spital / a few beds.
- **Almshouse variant:** **separate dwellings around a quadrangle + a common hall + chapel** ("Maison Dieu").
- **Up:** a great city hospital (100+ beds).
- **Fantasy:** a temple-hospital (healing rites — bible).

## 10. Function testers
- **F1** An aisled infirmary hall with beds **opening onto a chapel at the east end** (every bed sees the altar).
- **F2** Men/women segregated (separate halls or a partition + separate chapels).
- **F3** A dispensary.
- **F4** A warden/staff lodging.
- **F5** Bed count scaled to settlement (10–25 / 30–100 / 100+).
- **F6** *(almshouse)* separate dwellings around a court + a common hall + chapel.

## 11. Fixtures & assets needed (→ backlog)
Beds (rows), altar, chapel glazing, dispensary shelving, warden's furnishings. →
[`WantedAssetsBacklog.md`](../WantedAssetsBacklog.md).

## 12. Grounding ledger
| Claim | Provenance |
|---|---|
| aisled infirmary hall + east chapel in full view; monastic-infirmary plan; M/F segregation; Great Hospital 4 bays / 20 m aisles / ~16 beds; bed counts by settlement; almshouse quadrangle | CITED — [Hospitals & almshouses (Historic England)](https://historicengland.org.uk/research/inclusive-heritage/disability-history/1050-1485/hospitals-and-almshouses/); [The Great Hospital](https://www.thegreathospital.co.uk/history/medieval/buildings.html); [St Gall Plan](https://architecturehelper.com/blog/st-gall-plan-medieval-monastery-hospital-design/) |

## 13. Open questions / unknowns
- Bay/bed spacing along the aisle — derive from ~20 m / ~16 beds (≈1.25 m per bed bay) — confirm.
