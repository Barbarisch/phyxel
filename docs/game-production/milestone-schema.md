# `production/v2` schema reference

Field reference for `.phyxel/production.json` (the machine-readable production tracker) and the
`genre-templates/*.json` it is built from. Design rationale: [`README.md`](README.md) (§4, §6.1, §10).

`production.json` is **generated at scaffold time** by merging `core.json` + the selected genre
template(s), then **owned by the project** (committed, hand-editable, updated by the workflow /
the future `production(op=…)` MCP tool). Templates are the *source*; `production.json` is the *state*.

---

## `production.json` (the per-project state)

```jsonc
{
  "schema": "production/v2",
  "genres": ["survival"],          // one or more merged genre templates (core is always included)
  "stage": "concept",              // Axis A — see production-stages.md
  "strictPackaging": false,        // opt-in hard packaging gate (§6.4)
  "focus": "",                     // one line: what the current work is (shown in the digest)
  "milestones": {
    "<name>": { /* milestone entry, below */ }
  }
}
```

### Milestone entry

| Field | Type | Meaning |
|-------|------|---------|
| `status` | `todo` \| `in_progress` \| `done` \| `n/a` \| `blocked` \| `stale` | Workflow state. `stale` is set by the regression sweep (§8) when validation inputs change. |
| `required` | `L0`–`L4` | Target validation depth (from the template). |
| `validated` | `L0`–`L4` | Achieved depth. Complete = `status:done && validated ≥ required`. |
| `feel` | `n/a` \| `pending` \| `passed` | Axis C juice-pass state; only present on interactive milestones. Gates `vertical_slice` / `content_complete`. |
| `content` | object | Optional volume targets, e.g. `{ "recipes": { "target": 12, "current": 0 } }`. `current` is countable from data (§10.4). Gates `content_complete`. |
| `ordering_critical` | bool | Must be addressed *during* construction, not retrofitted (accessibility, localization_ready). Surfaced early by the process. |
| `combat_model` | `turn_based` \| `real_time` | On the `combat` milestone only — the genre parameter, not a fork. |
| `optional` | bool | Not required for completeness; excluded from `%complete`. |
| `reason` | string | **Required when `status:n/a`** — why it doesn't apply (auditor may challenge). |
| `note` | string | Free-form. |
| `validatedAt` | ISO-8601 | Durability (§8): when `validated` was last established. Absent until first validated. |
| `hash` | string | Durability (§8): content-hash over the milestone's validation inputs; a change flips `status` to `stale`. |
| `snapshot` | string | Durability (§8): snapshot id stamped at completion for rollback (`create_snapshot`). |
| `evidence` | string | Human-readable proof (screenshot ref, probe result, …). |

Fields beyond `status`/`required`/`validated` are added as the relevant phase lands — a Phase-0
scaffold writes only `status`, `required`, `validated` (+ `feel`/`content`/`ordering_critical`/
`combat_model`/`optional` when the template declares them). `validatedAt`/`hash`/`snapshot` arrive
with the durability phase; the schema tolerates their absence.

`%complete` = complete required (non-`optional`, non-`n/a`) milestones ÷ total required. Surfaced in
the SessionStart digest (Phase 1).

---

## `genre-templates/*.json` (the source templates)

`core.json` (always merged) additionally carries `"stages": [...]` (Axis A). Each genre file:

```jsonc
{
  "genre": "survival",
  "description": "…",
  "milestones": {
    "<name>": {
      "required": "L3",              // -> milestone.required
      "desc": "…",                   // human doc; NOT copied into production.json (lives in GAMEPLAN)
      "feel": "pending",             // optional -> milestone.feel
      "content": { "recipes": { "target": 12, "current": 0 } },  // optional -> milestone.content
      "ordering_critical": true,     // optional
      "combat_model": "turn_based",  // optional (combat only)
      "optional": true               // optional
    }
  },
  "content_targets": { "recipes": 12 },        // genre-level content bar (informational)
  "interaction_matrix_seed": [                 // seeds GAMEPLAN's interaction matrix (§10.3)
    { "a": "rain", "b": "campfire", "expect": "extinguishes" }
  ],
  "starter_scaffold": { "notes": "…" }         // hints for the starter game.json
}
```

**Merge rules (`build_production_json`):** start from `core.json` milestones (in order); for each
selected genre (in the order given), add its milestones, and if a milestone name already exists,
**the later template's fields override** (e.g. `rpg` → `action-rpg` overrides `combat` to real-time;
`survival`/`rpg` override `save_load` to `L4` save-integrity). `interaction_matrix_seed` entries and
`content_targets` are unioned into GAMEPLAN.md, not into `production.json`.

**Adding a genre = adding a JSON file here** — no code change. Keep `desc` present on every milestone
(it is the source for GAMEPLAN's milestone notes and the human-facing checklist).
