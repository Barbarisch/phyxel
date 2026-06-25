# StructureBrief — the mandatory intake for structure generation

> **No voxel is placed until a complete, grounded StructureBrief exists.** A request to "build
> structure X" is a request to *interview*, not to invent. This schema is the front door to the
> Structure Generation v2 pipeline (`docs/structure-generation/StructureGenerationV2.md`); the brief sits **above** the
> `BuildingProgram` — it is the requirements that *generate* the program. Run via the `/structure`
> skill.

## Why this exists

Underspecification is not license to make things up. "A house" omits ~40 form-defining decisions
(period, culture, climate, owner's status, material, construction method, site, scale, condition…),
and every one of them changes the voxels. The pipeline kept guessing and dressing guesses up as
decisions. The brief makes guessing impossible: every field is **either user-provided or a
grounded default with a citation that the user confirms** — never a third thing.

## The two-frame grounding rule (inherited from the project directive)

Every value is grounded in one of two frames, and the **period/setting decides which sources apply**:

- **Period-grounded** (wall/foundation thickness, materials, construction method, roof type, room
  program & sizes, massing, settlement norms): cite *historical/vernacular construction of the
  declared period & region*. The modern IRC does **not** size a medieval wall.
- **Anthropometric / gameplay-bounded** (door & ceiling clear height, stair rise/run, corridor
  width, furniture interaction heights): bounded by the **in-engine character**
  (`character_design_constraints.json`, 1.751 cubes). These may intentionally deviate from history
  for playability — a real medieval door was ~1.7 m; ours must clear ~2 m — and that trade is stated.

The `grounding-auditor` agent (`.claude/agents/grounding-auditor.md`) enforces this on every value.

## Intake style (locked)

**Ask the blocking fields; propose cited defaults for the rest.** The skill interrogates the
form-defining fields, then proposes grounded, *cited* defaults for the remainder in batches that the
user confirms or overrides. If a proposed default can't be grounded, the skill **stops and asks**.

## The fields (8 stages, ~43 fields)

Legend: **[B]** = blocking (must be answered before any generation) · **[D]** = propose a
grounded+cited default, user confirms · *branch* = appears only under a condition.

### Stage 0 — Setting *(the first gate; reframes all later sources)*
| # | Field | Kind | Source for a proposed default |
|---|-------|------|-------------------------------|
| 0.1 | `period` (era) | **B** | — (must be user-set) |
| 0.2 | `culture_region` | **B** | — |
| 0.3 | `tech_level` (tools, spans, glazing, metal) | **D** | derived from period+region; architectural-history refs |
| 0.4 | `magic.present` / `magic.affects_construction` | **D** | default false unless the world's lore says otherwise |
| 0.5 | `climate` / biome | **B** | — (drives roof pitch, wall mass, window size, materials) |

### Stage 1 — Function & status
| # | Field | Kind | Source |
|---|-------|------|--------|
| 1.1 | `function` (primary) | **B** | — |
| 1.2 | `secondary_use` (mixed-use) | **D** | function norms for the period (e.g. shop-under-dwelling) |
| 1.3 | `owner_status` (wealth / social class) | **B** | — (re-scales *everything*) |
| 1.4 | `occupants` (who / count / livestock) | **D** | household size by status+period; social history |
| 1.5 | `defensibility` | **D** | function (keep/manor → yes; croft → no) |

### Stage 2 — Site
| # | Field | Kind | Source |
|---|-------|------|--------|
| 2.1 | `site_context` (urban / rural) | **B** | — (town lot + party walls vs open) |
| 2.2 | `terrain` (flat / slope / hilltop / waterfront / cliff) | **B** | — (or read live terrain at the target) |
| 2.3 | `plot` (size / boundaries) | **D** | site_context + status |
| 2.4 | `orientation` (sun / street / view / wind) | **D** | climate + site_context |
| 2.5 | `access` (road / path / river) | **D** | site_context |
| 2.6 | `ground` (rock / soil / marsh → foundation type) | **D** | terrain; vernacular foundation practice |

### Stage 3 — Program *(branches by function & status)*
| # | Field | Kind | Source |
|---|-------|------|--------|
| 3.1 | `rooms` (required spaces) | **D** | function+status+period room program (a croft = 1 room + byre; a manor = hall + solar + service + …) |
| 3.2 | `adjacencies` & flow | **D** | architectural programming for the typology |
| 3.3 | `zones` (public / private / service) | **D** | typology |
| 3.4 | `special_spaces` (cellar / attic / hidden / shrine / workshop / well) | **D** | function+status |
| 3.5 | `entrances` (count / type) | **D** | function (defensible → single controlled entry) |
| 3.6 | `vertical_circulation` (stair / ladder / none) | **D** | stories + status (ladder for a loft, stair for a hall) |

### Stage 4 — Materials & construction *(branches by period + region + status + climate)*
| # | Field | Kind | Source |
|---|-------|------|--------|
| 4.1 | `structural_material` (timber / cob / stone / brick / …) | **B** | — (or proposed from period+region+status; confirm) |
| 4.2 | `wall_method` + `wall_thickness` | **D** | period construction (timber/daub ≈0.22 m; cob ≈0.67 m; castle masonry 2–6 m) — **see the sourced table in StructureGenerationV2 / structure_styles.json** |
| 4.3 | `roof_material` + structure (thatch / shingle / slate / tile / lead) | **D** | period+region+climate |
| 4.4 | `floor` (earth / timber / flagstone / tile) | **D** | status+period |
| 4.5 | `window_tech` (open / shutter / oiled cloth / leaded glass) | **D** | period+status (glass = wealth/late) |
| 4.6 | `finish_quality` (rough vernacular → finely dressed) | **D** | status |

### Stage 5 — Scale & form
| # | Field | Kind | Source |
|---|-------|------|--------|
| 5.1 | `footprint` (dims) | **B** | — (or proposed from program+status; confirm) |
| 5.2 | `stories` | **B** | — |
| 5.3 | `ceiling_height` (per story) | **D** | **anthropometric floor** (character) + **period/status target** |
| 5.4 | `massing` (rectangular / L / U / courtyard / organic) | **D** | culture+function+plot |
| 5.5 | `roof_form` (gable / hip / pitched / conical thatch) | **D** | region+roof_material+climate |
| 5.6 | `symmetry` / regularity | **D** | culture+status (vernacular = organic; noble = ordered) |

### Stage 6 — Condition & history
| # | Field | Kind | Source |
|---|-------|------|--------|
| 6.1 | `condition` (new / worn / aged / ruined) | **D** | default new+maintained unless the scene wants otherwise |
| 6.2 | `maintenance` | **D** | owner_status |
| 6.3 | `damage` (fire / war / decay) | **D** | scene/lore |
| 6.4 | `additions` (organic growth over time) | **D** | age+status |

### Stage 7 — Gameplay & engine
| # | Field | Kind | Source |
|---|-------|------|--------|
| 7.1 | `enterable` / walkable by the player | **B** | — |
| 7.2 | `interactive` (working doors / containers / NPCs) | **D** | function+enterable |
| 7.3 | `furnishing` (shell / furnished + clutter level) | **D** | function+status |
| 7.4 | `perf_budget` (voxel ceiling) | **D** | a standing default per structure class |
| 7.5 | `signature_features` (jettying, exposed framing, crow-steps, forge, altar…) | **D** | style+function |

**Blocking set (12):** `period, culture_region, climate, function, owner_status, site_context,
terrain, structural_material, footprint, stories, enterable` (+ `tech_level` confirmed). Everything
else is a cited default the user confirms. Branching prunes irrelevant fields (a one-room croft skips
most of Stage 3; a castle expands `defensibility` into its own sub-branch).

## The brief artifact (shape)

```jsonc
StructureBrief {
  "schema": "structure_brief/v1",
  "setting":   { period, culture_region, tech_level, magic, climate },
  "function":  { primary, secondary_use, owner_status, occupants, defensibility },
  "site":      { context, terrain, plot, orientation, access, ground },
  "program":   { rooms[], adjacencies[], zones, special_spaces[], entrances, vertical_circulation },
  "materials": { structural, wall_method, wall_thickness_m, roof_material, floor, window_tech, finish_quality },
  "scale":     { footprint:[w,d], stories, ceiling_height_m[], massing, roof_form, symmetry },
  "condition": { state, maintenance, damage, additions },
  "gameplay":  { enterable, interactive, furnishing, perf_budget, signature_features[] },
  // EVERY scalar above is stored with provenance:
  "_provenance": { "<field path>": { "value": …, "source": "user" | "<citation>", "confirmed": true } }
}
```

## Validation (the gate)

`brief.is_buildable()` is true **iff**: every **[B]** field is set; every **[D]** field is either set
or has a *confirmed* cited default; and **every scalar in `_provenance` has `source != null` and
`confirmed == true`**. Any field with `source: null`/`unsourced` BLOCKS generation. The `/structure`
skill will not call the realizer until this passes.

## How the brief drives the pipeline

`StructureBrief → BuildingProgram + StyleProfile selection + DimensionCanon refs → AssemblyPlan →
MicroCanvas → place`:
- `setting`+`materials` → select/derive the `StyleProfile` (period-correct thicknesses/materials).
- `function`+`status`+`program` → the room program (areas grounded per period+status, **not** modern
  residential figures).
- `scale` → footprint/stories/ceilings (ceilings honor the anthropometric floor).
- `site`+`terrain` → seating/excavation (P2) + foundation type.
- `gameplay` → doors/navmesh/furnishing/perf budget.

## Standing gaps this exposes (to ground next, cite-then-confirm)
- **Period room programs** (croft / longhouse / townhouse / manor hall / keep) — the modern
  1500/2100 sq ft figures do **not** apply to medieval dwellings.
- **Per-period `StyleProfile` library** (the current `structure_styles.json` is a seed; thicknesses
  pending the audit corrections — timber 0.222, cob 0.667, castle 2–6 m).
- **Foundation/plinth, stone-cottage thickness, stair rise/run, furniture sizes** — all still
  `NEEDS-RESEARCH` per the audit.
