# Arcane Emporium — Archetype Data Sheet

> Status: **DRAFT**. Schema: [`README.md`](README.md). **Fantasy — grounded to the world bible (Part 9).**

## 1. Identity
- **id:** `arcane_emporium`
- **function:** retail of magical goods — scrolls, wands, potions, components, curios
- **aka:** magic shop, curiosity shop, mage's emporium *(BG3: Sorcerous Sundries / Devil's Fee)*
- **group:** Commerce (Part 6) — **fantasy**
- **extends:** `townhouse` shell; the proprietor's quarters/lab above overlap `wizard_tower`
- **genre/period:** **fantasy** — the *structure* is grounded; the magic is grounded to the **world bible** (Part 9 / CC).

## 2. Essence
A **wondrous retail floor backed by a warded secure store**, the proprietor often a practitioner (lab/quarters
above). Defining quality: the counter + glowing wares + a **warded back-room** + the bible's magic rules.

## 3. Threat model / failure modes
- **Theft** — valuable **and dangerous** items → a **warded** store (beyond merely locked).
- **Magical hazard** — unstable items need containment (per the bible).
- Ordinary shop theft/fire.

## 4. Access tiers / zoning
- **T0** shop floor — counter + display of curios/scrolls/wands.
- **T1** staff/consult — the proprietor.
- **Warded store** — dangerous/costly, **magically sealed** (bible-defined).
- Proprietor's **lab/quarters above** (→ `wizard_tower` arcane program).

## 5. Required spaces (program)
| Space | Tier | Purpose | Required fixtures | Size |
|---|---|---|---|---|
| shop_floor | T0 | sell | counter, display shelving (glowing/animated wares), **`glow` lighting** | `to_ground` |
| warded_store | — | secure | a **bible-defined ward** + a locked masonry room | `to_ground` |
| consult / back-room | T1 | deal | desk, seating | `to_ground` |
| lab / quarters | — | the practitioner | see `wizard_tower` | above |

## 6. Adjacency & circulation rules
1. The **counter separates** customers from the (magical) stock (as any shop).
2. The **warded store is reached only via staff** space.
3. Dangerous items are **contained per the bible**.
4. A **street display** (the wonder draws custom); the lab/quarters above.

## 7. Construction & materials
- `townhouse` shell + **magical materials/lighting** — the **real `glow` material** + bible-defined arcane materials.
- A **warded store**: if the ward implies a force-field / non-Euclidean space, that's an **engine gap** (CC8) — otherwise it's a locked masonry room re-skinned.
- WANTED: glowing-wares props, arcane signage, defined arcane materials.

## 8. Signature / legibility
**Glowing/floating wares** in the window; an arcane/eldritch sign; a warded door; light spilling out (`glow`).

## 9. Status / period / setting scaling
- **Down:** a peddler's curio cart → a curiosity shop.
- **Up:** a grand multi-floor emporium with a tower above (BG3 Sorcerous Sundries) → fused with a `wizard_tower`.

## 10. Function testers
- **F1** A retail counter separating customers from the (magical) stock.
- **F2** A **warded/secure** store for dangerous + costly items (ward per the world bible — CC1/CC2).
- **F3** Magical lighting/materials **map to real engine materials** (`glow`, a defined arcane material), not invented visuals (CC6).
- **F4** Any **bible-licensed impossibility** (bigger-inside vault, floating display) is **flagged as an engine gap**, not faked (CC8).
- **F5** A street display + sign.
- **F6** *(if a practitioner)* a lab/quarters above per `wizard_tower`.

## 11. Fixtures & assets needed (→ backlog)
Counter, display shelving, glowing wares, warded-store door, arcane sign decal, `glow` light sources. →
[`WantedAssetsBacklog.md`](../../WantedAssetsBacklog.md).

## 12. Grounding ledger
| Claim | Provenance |
|---|---|
| the structural shell | REUSE-CANON — `townhouse` (grounded) |
| magic goods / wards / arcane materials | BIBLE-SOURCED (Part 9 / CC) |
| magical light = the `glow` material | exists in `materials.json` (real) |
| retail magic shop | GENRE-FLAG — fantasy conceit (CC7) |
| non-Euclidean / force-field wards | ENGINE-GAP flag (CC8) — not faked |
| sizes | `to_ground` |

## 13. Open questions / unknowns
- Does a "warded store" need a new engine feature (containment/force-field) or is it a re-skinned locked room? — resolve to the latter unless the bible demands more.
- The lab/quarters above: reference `wizard_tower` once that sheet exists.
