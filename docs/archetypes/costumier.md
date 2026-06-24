# Costumier / Disguise Shop — Archetype Data Sheet

> Status: **DRAFT**. Schema: [`README.md`](README.md). *(BG3: Facemaker's Boutique.)*

## 1. Identity
- **id:** `costumier`
- **function:** make/sell costumes, masks, disguises, fancy dress
- **aka:** mask-maker, mummer's outfitter, disguise shop
- **group:** Entertainment & vice (Part 6)
- **extends:** `tailor_weaver` / `townhouse` shell
- **genre/period:** a **dedicated retail costumier is late/early-modern + fantasy** — medieval = a guild pageant **wardrobe** + mummers, not a shop. GENRE-FLAG.

## 2. Essence
A **tailor's shop specialised for disguise/theatre** — a display front + a fitting room + a workshop + a costume
store. Defining quality: display of finished costumes/masks + a **private fitting room** + the maker's workshop.

## 3. Threat model / failure modes
- **Light** — fine work (reuse `tailor_weaver`).
- **Theft** — rich costumes (secure store).
- **The disguise function** — a private fitting room.

## 4. Access tiers / zoning
- **T0** shop — display of costumes/masks, counter.
- **T1** fitting room — private (a mirror).
- Workshop (well-lit) + costume store; dwelling above.

## 5. Required spaces (program)
| Space | Tier | Purpose | Required fixtures | Size |
|---|---|---|---|---|
| shopfront | T0 | display/sell | display (mannequins), counter | `to_ground` |
| fitting room | T1 | try on | a **mirror** (the `Mirror` material exists), seating | `to_ground` |
| workshop | — | make | cutting table, bench, mask-making, **good light** | reuse `tailor_weaver` |
| costume store | — | keep stock | dry, racked | dry/secure |
| dwelling | — | home | dwelling fixtures | above |

## 6. Adjacency & circulation rules
1. Display **to the street**; the **fitting room private** off the shop.
2. The **workshop is well-lit** (reuse the tailor light rule).
3. The **costume store dry + secure**.

## 7. Construction & materials
- `tailor_weaver`/`townhouse` shell + good light + a **mirror** (real `Mirror` material) + display.
- WANTED: costume racks, mannequins, masks, trade sign.

## 8. Signature / legibility
Costumes + masks in the window; a **theatrical/mask sign**; a mirror.

## 9. Status / period / setting scaling
- **Medieval:** a guild **pageant wardrobe** (not a shop).
- **Up:** a costumier / mask-maker → a grand theatrical/disguise boutique (BG3 Facemaker's).
- **Fantasy:** a disguise-magic shop (overlaps `arcane_emporium`; bible).

## 10. Function testers
- **F0 (genre):** a dedicated retail costumier is late/fantasy; medieval = a guild pageant wardrobe (flag).
- **F1** A display of finished costumes/masks at the front.
- **F2** A **private fitting room** (with a mirror).
- **F3** A well-lit workshop (cutting/sewing/mask-making — reuse `tailor_weaver` light).
- **F4** A dry, secure costume store.
- **F5** A trade sign.

## 11. Fixtures & assets needed (→ backlog)
Costume racks, mannequins, masks, mirror, cutting table, counter, sign. →
[`WantedAssetsBacklog.md`](../WantedAssetsBacklog.md).

## 12. Grounding ledger
| Claim | Provenance |
|---|---|
| workshop + light | REUSE-CANON — `tailor_weaver` (loom/light cited there) |
| dedicated retail costumier | GENRE-FLAG — late/early-modern + fantasy |
| mirror | `Mirror` material exists in `materials.json` |
| **sizes** | `to_ground` |

## 13. Open questions / unknowns
- Model the medieval **guild pageant wardrobe** as the period-correct alternative? (recommended for a medieval brief).
