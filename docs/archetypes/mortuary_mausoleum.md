# Mortuary / Mausoleum — Archetype Data Sheet

> Status: **DRAFT**. Schema: [`README.md`](README.md). Links Part 8 (`place_crypt`) + placer #32 (`place_graveyard`).

## 1. Identity
- **id:** `mortuary` / `mausoleum`
- **function:** prepare the dead (mortuary) and/or house the dead (mausoleum/tomb)
- **aka:** charnel house, ossuary, tomb-house, sepulchre
- **group:** Civic & institutions (Part 6) — often a **church/graveyard** adjunct
- **extends:** a small ritual building over/beside a **crypt** (Part 8)
- **genre/period:** real (charnel houses, mausolea, tomb chapels).

## 2. Essence
A building **for handling or housing the dead** — a mortuary (laying-out + preparation) and/or a mausoleum (a
dignified tomb-house over/beside a crypt). Defining quality: reverence + separation (hygiene) + (mausoleum) the
security of remains.

## 3. Threat model / failure modes
- **Reverence / dignity + the rite** (the primary driver).
- **Hygiene** — the dead separated from the living.
- **Grave-robbing** — (mausoleum) secure remains + any grave goods.

## 4. Access tiers / zoning
- **Mortuary:** a laying-out room + a cool store.
- **Mausoleum:** a tomb chamber (above a sealed **crypt**, Part 8) + an entrance gate/grille + (often) a small altar.

## 5. Required spaces (program)
| Space | Purpose | Required fixtures | Size |
|---|---|---|---|
| laying-out room *(mortuary)* | prepare the dead | a slab/bier, cool, separated | `to_ground` |
| tomb chamber *(mausoleum)* | house the dead | sarcophagi / wall niches | `to_ground` |
| crypt *(below)* | interment | (reuse Part 8: loculi 0.4–0.6 × 1.2–1.5 m) | REUSE Part 8 |
| altar / prayer space | prayers for the dead | a small altar | `to_ground` |
| entrance | controlled | a gate/grille (secure) | — |

## 6. Adjacency & circulation rules
1. Sited **by the church/graveyard** (#32); the **crypt below** (Part 8).
2. **Oriented per the rite** (e.g. east).
3. The **laying-out room is separated** (hygiene).
4. **Secure against grave-robbing** (a gated/grilled, lockable entrance).

## 7. Construction & materials
- Stone (permanence + security); a sealed crypt below; carved memorials.
- WANTED: sarcophagi, wall niches (loculi), tomb effigies/sculpture, gate/grille, marble.

## 8. Signature / legibility
A small, solid, **stone** building among graves — a tomb-house or charnel; a gated entrance; carved memorials.

## 9. Status / period / setting scaling
- **Down:** a charnel/ossuary (bone store) beside the churchyard.
- **Up:** a noble **mausoleum** / a tomb chapel over a family crypt.
- **Fantasy:** a warded tomb / a lich's sepulchre (bible) — guards against grave-robbers/undead.

## 10. Function testers
- **F1** *(mortuary)* a laying-out room (slab/bier), cool + separated.
- **F2** *(mausoleum)* a dignified tomb chamber over/beside a **sealed crypt** (Part 8).
- **F3** Sited by the church/graveyard, oriented per rite.
- **F4** **Secure** against grave-robbing (a gated/lockable entrance).
- **F5** A place for prayers (altar) where the rite expects it.

## 11. Fixtures & assets needed (→ backlog)
Bier/slab, sarcophagi, wall niches (loculi), tomb effigies/statues, gate/grille, marble. →
[`WantedAssetsBacklog.md`](../WantedAssetsBacklog.md).

## 12. Grounding ledger
| Claim | Provenance |
|---|---|
| crypt / loculi dimensions | REUSE-CANON — Part 8 (Catacombs of Rome, cited there) |
| graveyard siting + orientation | REUSE-CANON — placer #32 + checklist R |
| charnel/mausoleum/tomb-chapel as real types | general medieval funerary practice |
| **sizes** | `to_ground` |

## 13. Open questions / unknowns
- Standalone mausoleum vs a crypt-only-under-the-church — which is the default by status.
- Charnel/ossuary capacity + form — `to_ground`.
