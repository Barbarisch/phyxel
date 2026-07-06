# Trim Grounding — finish_forge P2 `place_trim` + P1 remainder

_Grounding-auditor output, 2026-07-06 (pre-implementation, per the ForgePattern contract:
grounded inputs before code). Grid: 1 cube = 1 m, 1 subcube = 1/3 m ≈ 0.333 m,
1 micro = 1/9 m ≈ 0.111 m. Every GROUNDED row cites a source; every gap says NEEDS-RESEARCH
rather than inventing a number. ⚠ modern-analog = physically-plausible modern trade figure,
not a period citation — flagged, not passed off as vernacular._

## GROUNDED — usable now

| Item | Real value + source | Grid mapping | Notes |
|---|---|---|---|
| Plinth height above grade (cob/earth walls) | min 300 mm, 460–610 mm in high-rainfall (thiscobhouse.com stone-foundation guidance; consistent w/ SPAB *Control of Dampness*) | **3 micro = 0.333 m** default (+11% vs 300 min); 5 micro = 0.555 m wet-climate variant | genuinely closest grid rungs, not grid-convenience |
| Plinth chamfer angle | 45° conventional (Plean Precast cast-stone plinth spec) | 1×1-micro diagonal step **is** 45° | rare real-convention/grid coincidence — flagged so it isn't miscounted as convenience |
| Corner quoin block | 450 × 300 × 145 mm — **reclaimed period stock** (Britannia Stone, reclaimed quoins) | long leg 4 micro (−1.3%), short leg 3 micro (+11%), thickness 145 mm → 1 micro (**−23%, the grid floor** — solution-auditor 2026-07-06: state the error, don't gloss it) | **RECONCILIATION NOTE (auditor):** the shipped pass uses this 1-micro block thickness for BOTH the course height AND the outward projection. The projection-from-wall-plane is still NEEDS-RESEARCH #3 below — the block thickness is a disclosed INTERIM proxy for it (a quoin can't project more than its own thickness), not a resolution of #3. If #3's real figure comes back smaller than 111 mm it is sub-grid anyway (same honest bound as the window sill) |
| Timber post/rail section | 150–300 mm structural members (Wikipedia *Timber framing*) | **2 micro (0.222 m)** humble/common; **3 micro (0.333 m)** principal/high-status | both rungs inside the sourced range |
| Close-studding stud width | 150 mm, "six-inch studs spaced six inches apart" (Wikipedia *Close studding*) | real value sits at 1⅓ micro — **neither rung close** (1 micro −26%, 2 micro +48%) | honest MISMATCH: use 2 micro and document the error |
| Common-stud spacing | gap 150–600 mm max for "close" (same source) | 3 micro (tight/high-status) … 5 micro (loose) | use as a **wealth-tier range**, not one number |
| Sill beam section | same 150–300 mm general-member range (no sill-specific source) | 2–3 micro | GROUNDED but thin — one citation class removed; revisit w/ Hewett/Brunskill |
| Jamb stock (P1 re-audit) | ~100–150 mm ordinary cottage door post (Timber framing member range, low end) | **1 micro = 111 mm, ≤10% error — GROUNDED for humble styles** | corrects GroundingGaps #8's wrong reference class (38–50 mm = modern door lining); high-status → 2–3 micro |
| Lintel depth (timber) | 150–300 mm (same member range) | 2 micro = 0.222 m, inside range | on the light side of midpoint; acceptable |
| Fascia depth | 150–225 mm (self-build.co.uk fascia guide) ⚠ modern-analog | 2 micro = 0.222 m | fascia THICKNESS (19–25 mm) must NOT get its own micro layer (1 micro is 4–6× too thick) — fold into cornice silhouette / texture |
| Reveal depth | derived: `wall_thickness − frame_setback` — wall thickness per style already grounded (`structure_styles.json`) | n/a | flag any placer that hardcodes a reveal independent of the style wall |

## Soft mismatches / bounded conveniences

- **Stone lintel depth** 2 micro vs the engineering rule (~120 mm for a 0.9 m span): oversized by the
  strict rule, but real stone lintels often run a full course height — resolve with the string-course
  citation below before judging.
- **Window sill projection** 1 micro (111 mm) vs real 25–102 mm (Gobrick TN36; dynamicstonetools):
  **grid-floor convenience, honestly bounded** — the real value is smaller than the finest grid rung.
  Sill slope ≥15° needs a stepped-chamfer geometric solution, not a flat quantization.
- **Classical cornice proportion** (height = 1/15–1/18 of building height, jlconline): classical rule —
  do NOT apply to medieval vernacular styles.

## NEEDS-RESEARCH — blocking list (do not invent)

| # | Item | Consult |
|---|---|---|
| 1 | Plinth height, stone/ashlar walls | Historic England *Practical Building Conservation: Stone*; Clifton-Taylor |
| 2 | Plinth projection from wall face | Historic England *PBC: Brick, Terracotta & Earth* |
| 3 | Quoin projection from wall plane | Historic England *PBC: Stone*; stonemasonry detailing manual |
| 4 | Quoin long-short alternation ratio | Taylor & Taylor, *Anglo-Saxon Architecture* |
| 5 | String-course height + projection | Historic England *PBC: Brick…*; a measured-drawing survey |
| 6 | Bargeboard thickness | SPAB Technical Advice Notes (timber joinery) |
| 7 | Vernacular eave overhang (thatch/tile) | Historic England thatch guidance; regional vernacular survey (only figure found, 300–450 mm, is ⚠ modern residential) |
| 8 | Timber brace cross-section | Hewett, *English Historic Carpentry* (page-level; modern strap-brace figures are the WRONG reference class) |
| 9 | Sill-beam-specific section | Hewett or Brunskill, *Timber Building in England* (1985) |
| 10 | Stone lintel vs coursing | needs #5 first |
| 11 | Door threshold height | Hewett; or an in-situ door-sill excavation (cf. Wharram Percy, already cited in this codebase) |

**Bay width / post spacing:** already grounded as `bay_length ~4 m` in `DimensionReference.md` —
reuse, don't re-derive a competing number for trim.
