# Game Production Workflow — Design Plan (v2)

> **Status:** DESIGN — not yet implemented. Canonical design entry for the **production-completeness
> spine**: the per-project tracker, milestone validation, session onboarding, and guided process that
> carry a Phyxel game from empty project to *shippable* — and keep it shippable as it changes.
>
> **Reads before this:** [`docs/GameDevWorkflow.md`](../GameDevWorkflow.md) (the workflow *plumbing* —
> CLI, ports, hook, plugin — all already shipped) and [`docs/GameCreationGuide.md`](../GameCreationGuide.md)
> (the authoring how-to). This doc is the layer **on top**: "how do we know the game is done, how does
> every session know what's left, and how do we stop finished work from silently rotting?"
>
> **Discipline note:** same culture as [`docs/structure-generation/`](../structure-generation/README.md)
> — validation is a *planned deliverable* (L1–L4), red-before-green, auditor-confirmed. We extend it
> from *structures* to *whole games*.
>
> **v2 changelog:** v1 was a flat checklist + per-item L0–L4 ladder. Research (§2) showed that model
> (a) conflates three axes the industry separates, (b) treats "done" as permanent, and (c) has no rung
> for *feel* or proof of *completability*. v2 restructures into **three orthogonal axes** (§4) and adds
> two subsystems: **durability/regression** (§8) and **completability + adversarial playtest** (§9).

---

## 1. The vision (restated)

The standard usage path we're designing for:

1. User launches the engine, creates a new project → a self-contained game project in its own directory
   (already works: `phyxel new` / `create_project.py`).
2. User opens **one or more Claude sessions** in that directory and builds the game **in steps, across
   many sessions**.
3. **Every session is automatically brought up to speed** on what is and isn't done.
4. The process is **interactive, consistent, and convergent** — it drives toward a *fully functioning,
   feature-complete, packageable* game against a **known standard**, and it **notices when previously
   finished work breaks**.

The user's off-the-top checklist — goals/design/genre/demographic, high-level story, opening cinematic,
main menu, intro level, gameplay, win conditions, credits — is *exactly* the standard shippable-game
checklist from GDD literature (§4). We formalize it and add the axes practitioners keep hitting.

---

## 2. Research base — this is a genuine gap, with sharp edges

We surveyed **15+ engine MCP servers** (Godot/Unreal/Unity/Blender), the **consumer AI game builders**
(Rosebud, Astrocade, SEELE, Summer, GDevelop, Buildbox, Ludus), the **multi-agent / academic pipelines**
(GameGPT, gamestudio-subagents, StraySpark, RuleSmith), **real studio production practice** (Tim Cain's
phase/quality split, Supergiant content-lock, console cert), the **game-feel discipline** (Swink; "Juice
it or lose it"), and **four real "weekend AI full-game" builds** (Stefan Vaskevich's UE 5.8 fox runner;
"From C64 to Claude"; Chier Hu's survey; the HermeticOrmus patterns repo).

### 2a. The headline

**No engine MCP server and no consumer builder models project progress durably.** They are stateless
command surfaces or forward-only generators. Nobody persists a machine-readable "how far toward shippable
/ what's next / what just broke" artifact. Phyxel already practices the missing discipline for *structures*
(`ValidationLedger`, required-vs-current depth, red-before-green). Porting it to whole-game production —
**and adding durability** — puts Phyxel ahead of everything surveyed on the axis they all lack.

### 2b. The convergent real-world pipeline (4 independent builds agreed)

| Practice | Ours (existing/planned) |
|---|---|
| **CLAUDE.md = design doc + known-workarounds** ("a minute on CLAUDE.md saves ten correcting output") | `GAMEPLAN.md` (design) + per-project `CLAUDE.md` (gotchas) |
| **Git/commit checkpoint after every milestone** (the revert system) | Bind milestone completion to a **snapshot** (`create_snapshot` exists) — §8 |
| **MCP screenshot / play-in-editor loop** — cited as *"the single most useful thing in the whole setup"* | `screenshot`/`get_visual_diagnostic` + `--project` runtime = our L3/L4 — §6.5 |
| **External asset-gen, then wire in** | BlockSmith/templates + the asset track — §4c |

**The universal bottleneck, phrased identically by everyone: AI can't judge runtime *feel/fun* and can't
natively "press play."** Ground-truth quote: a leveling cadence tuned to *every 3.9s* when the fun range
is 10–30s — *"tests pass ≠ fun."* **Audio** and **level-design-for-fun** are the most-dropped scope. The
"72 hours" build was ~**15 minutes of actual Claude interaction**; the rest was assets + iteration —
the agent's real job is a small, high-leverage logic slice, not everything.

### 2c. Patterns stolen (folded into the design)

Two-tier state read-back + "numbers for structure, pixels for appearance" (Blender); state as read-only
**MCP resources**, paginate trees (Unity/hi-godot); **structured verification** not just screenshots
(mcp-unreal JSON error counts, delta logs); **creator→validator pairing + human checkpoints** (GameGPT,
StraySpark — fully-autonomous 4–5-agent runs are documented *unreliable*).

### 2d. ⚠️ Adjacent problem (tracked, not solved here)

MCP tool-selection accuracy drops past ~20–30 tools; **Phyxel's ~300-tool surface is in the danger
zone**. This design adds **one** rollup tool (§6.2); the broader ~300-tool consolidation (`op=`/`action=`
rollups + per-session tool-group gating) is a **separate workstream**.

### 2e. Explicitly out of scope (for now)

Multiplayer/backends, monetization/IAP, remix/share libraries, console certification (TRC/TCR/Lotcheck),
and live-ops. These are real in the consumer/studio landscape but not for a **single-player voxel game**
built by Claude sessions. Left as optional future milestones (§4e), not core.

### 2f. Design principles (north stars — the tracker must not become bureaucracy)

Distilled from the AI-first engines that work best (esp. **Capybara 2.5D** — an open-source AI-first
engine + asset MCP whose stated goal is *"remove the boring stuff so devs focus on the fun — gameplay
and story"*):

1. **Minimize toil, don't add it.** The tracker exists to make the *boring-but-required* work (menus,
   save/load, accessibility, packaging) **impossible to forget** and, where possible, **auto-scaffolded
   and auto-validated** — so the human/agent spends attention on gameplay and story, not bookkeeping. If
   a milestone can be scaffolded or checked automatically, it must be; the checklist is a safety net, not
   a form to fill.
2. **Small, stable, well-documented agent surface.** Capybara puts its *entire* public API in one file
   (`Game.ts`) "so the agent can hold it in context." Same reason we use **one `production(op=…)` rollup +
   one `phyxel://production` resource** (§6.2) and flag the ~300-tool surface as a hazard (§2d).
3. **Robust to weaker models.** Capybara's demo deliberately used a *weaker* model "to show the floor."
   Our generators (do the spatial work the LLM is bad at) + validators (catch the errors) + precise
   GAMEPLAN specs make the workflow **degrade gracefully** rather than depend on top-tier model vision.
4. **Assets are wired game *objects*, not raw art** (see §4c) — the single most time-saving idea in the
   Capybara MCP.
5. **Ship a recipe library, not just a checklist** (see §4d) — the "how," so the process is *consistent*
   across sessions, which was the original ask.

---

## 3. What exists vs. what's missing

**Already shipped (`main`) — do not rebuild:** `phyxel` CLI, per-project ports, `phyxel up` SessionStart
hook, per-project `CLAUDE.md`, the 6 `phyxel-gamedev` skills, `/feedback`→`/triage-feedback`.

| Capability | Today | Target |
|---|---|---|
| "Is the game complete?" | `validate_game_definition` = **schema only** (types + material names) | Milestone completeness + L1–L4 + feel |
| Packaging gate | `package_game` requires only a **binary** | Soft completeness gate + report; opt-in hard gate |
| Session onboarding | `phyxel up` **only launches the engine** | Hook injects a **live status digest** |
| Progress artifact | **none** | `production.json` + `GAMEPLAN.md` |
| Durability | **none** — no notion of work breaking | Staleness + regression sweep + snapshots (§8) |
| Completability | **none** | Whole-game reachability/softlock + adversarial playtest (§9) |

Existing hooks we build on: the declarative `triggers[]` win-conditions (`show_victory`/`show_credits`/
`transition_scene`/`quit_game`), menu scenes + shell `ScreenState`, `create_snapshot`/`restore_snapshot`,
`TraversalProbe` (structure-gen L3), `get_render_stats`/`get_visual_diagnostic`, and `package_game.py`'s
`REQUIRED_RESOURCES` list.

---

## 4. The model — three orthogonal axes

v1's flat list conflated three things real production separates (Tim Cain's scope-vs-quality split). v2:

- **Axis A — Production stage** (project-wide gate; §4a): how *mature* is the whole game.
- **Axis B — Milestone completeness** (per-feature; §4b–4e): does each required piece exist and function,
  measured on the L0–L4 ladder.
- **Axis C — Feel / polish** (per-interactive-milestone; §4f): does it feel good — *orthogonal* to L0–L4,
  never a higher L-rung.

A project is "shippable" only when **stage = shippable AND all required milestones complete at their
required depth AND feel-gated milestones passed their juice pass**. The three are tracked separately so a
project can't read "done" while `credits` is L4 and `core_loop` is a prototype.

### 4a. Axis A — production stages (ordered, with exit criteria)

Condensed from the industry spine (pre-production → prototype → first-playable → vertical-slice → alpha →
beta → RC → gold) to five stages that fit AI-driven single-player builds:

| Stage | Exit criterion (gate) |
|---|---|
| `concept` | `GAMEPLAN.md` filled: genre, demographic, core-loop, win/lose stated |
| `vertical_slice` | **one** complete slice at final quality — a level playable start→win with real (not placeholder) core loop, feel pass done. *Prove depth before breadth.* |
| `feature_complete` | every required milestone present (may be rough); **feature-freeze** |
| `content_complete` | all content/levels in, placeholders gone, **content-volume targets met (§10.4)**, balance tuned; **content-lock** |
| `shippable` | perf target met, QA + adversarial playtest clean, packaged; **code-freeze** |

The **`vertical_slice`-before-scale gate is the most important addition** — it directly counters the
consumer-tool failure mode of "every screen exists once but nothing is deep," and matches how the real
72-hour build actually worked (one polished slice, not breadth).

### 4b. Axis B — CORE milestones (all games)

| Milestone | "Done" means | Req. depth |
|---|---|---|
| `design_brief` | `GAMEPLAN.md` filled | L1 |
| `world` | A playable world/level loads | L4 |
| `player` | Player + camera placed and controllable | L4 |
| `level_playability` | Walkable paths, collision boundaries, death/out-of-bounds volumes, gating (reachable-means-walkable) | L3 |
| `core_loop` | Primary mechanic implemented and playable | L4 |
| `win_condition` | Terminal trigger wired (`show_victory`/`transition`/`quit`) | L4 |
| `lose_condition` | Fail/death/respawn path wired | L3 |
| `main_menu` | Menu scene / shell with Start/Options/Quit | L4 |
| `pause_menu` | Resume/settings/quit-to-menu | L3 |
| `options` | Settings (audio/graphics/controls) | L2 |
| `hud` | In-game HUD (health/score/objective) | L3 |
| `intro` | Opening: cinematic or intro sequence | L3 |
| `tutorial` | **Teaches the core loop** (contextual, one mechanic at a time, repeatable) — distinct from `intro` | L3 |
| `victory_screen` | Win/results screen | L3 |
| `game_over_screen` | Fail screen | L3 |
| `audio` | Menu + gameplay music + core SFX (the most-dropped scope — named explicitly) | L2 |
| `credits` | Credits screen/scene | L2 |
| `save_load` | Progress persistence (or explicit `n/a` w/ reason) | L3 |
| `difficulty_balance` | Win/lose reachable *and tuned*; difficulty options where relevant (balance = a play distribution, ties to §9) | L4 |
| `accessibility` | Remappable controls, subtitles, readable text size, no color-only info, flash safety, separate volume sliders | L2 |
| `localization_ready` | **Ordering-critical:** strings externalized w/ stable IDs, Unicode font, ~35% text-expansion-tolerant UI — enforced *before* strings are authored | L2 |
| `perf_target` | Meets the project FPS/load budget (`get_render_stats`) | L4 |
| `qa_pass` | Playtest checklist + adversarial playtest clean (§9) | L4 |
| `package` | Builds + packages standalone clean | L4 |

> **`localization_ready` and `accessibility` are ordering-critical** — un-retrofittable if the agent
> hardcodes strings / bakes color-only cues during construction. The process (§7) surfaces them *early*,
> not at the end.

### 4c. Asset / art track (was implicit in v1)

The dominant time-sink in every real build was assets. Named explicitly (Phyxel mapping in parens):

- `environment_art` — world/structure assets present at quality (templates / `build_structure` / BlockSmith).
- `character_art` — player/NPC models + **rig/anim** (`.anim`, character pipeline).
- `materials_textures` — coherent material set (materials.json / texture atlas).

**An asset is done when it's a wired game *object*, not raw art.** The biggest time-saver in the Capybara
asset MCP was that it *"outputs game objects, not just PNGs"* — meshes/sprites arriving with the metadata
that makes them usable: collision/obstacle data, depth/y-sort, placement anchors, bounding volumes. So an
asset milestone's L2/L3 bar is **"placed and wired with its game-object metadata"** — not "the model
exists." Phyxel already embodies this (templates carry `featureAt`/`assembly_plan` metadata, chunk
occupancy, sub-voxel detail); the tracker holds assets to that bar.

**Stateful props / prop-sequences are their own content type.** Capybara ships *"prop sequences like crop
lifecycles"* and *"VFX for interactive state transitions (a fire pit)."* Many survival/RPG assets have
**states** — crop seed→grown→harvested, campfire lit/unlit, door open/closed, chest closed/open — and are
"done" only when **every state exists and is wired to its transition**. This ties directly to the
interaction matrix (§10.3, state transitions) and content manifests (§10.4). For a farming/survival game
the crop-lifecycle asset is a core content type, not a cosmetic.

**Reuse counts as done.** The real builds reused owned water/sky/mountain assets and engine templates
freely. A validator must accept "reused a template/material" as satisfying an asset milestone — never
force generation.

### 4d. Genre templates = checklist **+ starter scaffold**

The single biggest accelerator in the wild was starting from a genre template (Unreal's 3rd-person
platformer template). So a genre template is **not just a milestone list** — it ships a **starter
`game.json` scaffold** (player controller, camera, input, a bootstrap level) plus its extra milestones:

- **Platformer:** `level_progression`, `checkpoints`, `hazards`, `collectibles`.
- **RPG / adventure:** `story_arcs`, `dialogue`, `quests`, `inventory`, `progression`, `npcs`.
- **Shooter / action:** `combat`, `weapons_equipment`, `enemy_ai`, `resource_economy`.
- **Sandbox / creative:** `building_tools`, elevated `save_load`, marks `win_condition` candidate-`n/a`.
- **Narrative / walking-sim:** `story`, `dialogue`, `cinematics`, `scene_flow`.

Templates + scaffolds live as data in `docs/game-production/genre-templates/*.json` (Phase 0). **Multiple
genres may be merged** (union of milestones) — real games are cross-genre.

**Recipes — the "how," so the process is consistent (principle §2f.5).** Beyond the *what* (milestone
list) and the *starter* (scaffold), ship a **recipe library** of step-by-step build playbooks for common
features — the pattern Capybara uses to great effect (`docs/recipes/`: farming-sim, rpg-game-design,
rpg-quests-inventory, save-load, enemy-ai-waves, season-atmosphere, combat, inventory, spawning,
npc-pathfinding…). Each recipe is a short, Phyxel-specific "to build X: these MCP calls / these GAMEPLAN
entries / this validator." They make every session build a given feature the *same* way (the original ask
for a "consistent process"), and they're what the `/gamedev-next` guided skill (Phase 8) draws from. Our
existing `phyxel-gamedev:*` skills are the domain layer; recipes are the *task* layer beneath them. Prime
the library toward the target genres: `farming-survival-loop`, `needs-and-hunger`, `rpg-quest-chain`,
`dialogue-with-skill-checks`, `turn-based-encounter`, `real-time-combat`, `loot-and-itemization`,
`save-integrity`, `interaction-matrix-setup`.

> **The primary target genres — survival · RPG/D&D · action-RPG — get a full deep dive in §10**, incl.
> their milestone sets, the systems-interaction matrix, content manifests, and save-integrity.

### 4e. Optional / future milestones (off by default)

`distribution` (share/publish beyond a local package), `brainstorm_intake` (a pre-`design_brief` ideation
stage — Astrocade-style), `multiplayer`, `monetization`. Available to add per project; not in CORE.

### 4f. Axis C — feel / polish (orthogonal to L0–L4)

Functional validation is *provably blind to feel*: a `TraversalProbe` confirms a staircase is *reachable*,
never *satisfying*; a milestone can be 100% L4 and viscerally dead (silent inputs, linear snaps, no
impact weight). And feel is the "last 10%" an AI whose checklist tops out at "functional" will **always**
skip. So feel is a **separate flag** on interactive milestones, with a **"juice pass" gate**:

- Sound on **every** input and state-change; easing/tweening (ban linear snaps); impact feedback
  (screenshake + hit-stop + particles) on hits/pickups; camera kick/zoom where apt; and where there's
  movement, **coyote time (~6 frames)** + **input buffering (~8 frames)**.

`feel: "n/a" | "pending" | "passed"` per interactive milestone. It gates `vertical_slice` and
`content_complete`, never blocks early functional work.

### 4g. Validation depth ladder (unchanged from v1, applied to Axis B)

- **L0** not started · **L1** artifact exists · **L2** structural check on real output · **L3** functional
  in-engine (driven, evidence captured) · **L4** live runtime playtest verified.

Each milestone declares its **required depth**. Complete = `status:done && validated ≥ required`,
red-before-green, auditor-confirmed. A per-project generated **`ValidationLedger.md`** mirrors structure-gen.

---

## 5. Validators — how the engine checks a milestone

A `MilestoneValidator` registry (one per milestone type) each declares **the max depth it reaches
automatically** and returns **structured evidence** (not just a screenshot — the mcp-unreal pattern):

- `main_menu` → **L2** auto: menu scene / `mainmenu_screen.json` present w/ Start/Options/Quit. **L3**:
  launch, navigate, screenshot.
- `win_condition` → **L2**: a `triggers[]` terminal action referencing valid targets. **L3**: scripted
  fire → assert victory state. **L4**: playtest reaches it (§9).
- `level_playability` → **L3**: `TraversalProbe` at level scale — a character-box can walk the intended
  path; out-of-bounds volumes catch falls; gated areas unreachable until the gate opens.
- `audio` → **L2**: referenced music/SFX resolve on disk and are wired (menu + gameplay).
- `localization_ready` → **L2**: no hardcoded UI strings (scan), stable-ID string table present.
- `qa_pass` → **L4** only (§9).
- `interaction_matrix` (systemic genres) → **L3**: each declared "system A × B → effect" runs as a
  scripted in-engine scenario; plus **resource-loop-closure** (no consumable sink without a reachable
  source). §10.3.
- `save_integrity` (RPG/survival) → **L3**: `save → reload → deep-diff every persisted subsystem == identical`. §10.5.
- content milestones w/ a `target` count → **L2**: `current` counted from data (`resources/rpg/`,
  templates, recipes) vs target. §10.4.

**Automatic depth is capped and honest** — a validator that reaches only L2 says so; L3/L4 needs the
driven engine / playtest. **No self-certification of L3+**: confirmed by an **auditor sub-agent** or
scripted validator, never the building session's say-so. Validators start **static (L1/L2)** in Phase 3,
grow **runtime (L3/L4)** in Phase 5.

---

## 6. Architecture

### 6.1 The two artifacts

**`.phyxel/production.json`** — machine state; source of truth for gates, onboarding, durability. Committed.

```jsonc
{
  "schema": "production/v2",
  "genres": ["platformer"],            // one or more (merged)
  "stage": "vertical_slice",           // Axis A
  "strictPackaging": false,            // opt-in hard gate
  "focus": "wiring the boss fight",    // one line: current work
  "milestones": {
    "level_playability": {
      "status": "done", "required": "L3", "validated": "L3",
      "feel": "passed",                          // Axis C
      "validatedAt": "2026-07-12T18:04:00Z",     // durability (§8)
      "hash": "sha256:…scope-of-inputs…",        // invalidation key
      "snapshot": "snap_2026-07-12_1804",        // rollback point
      "evidence": "TraversalProbe: path start→star OK; OOB volume catches fall"
    },
    "win_condition": { "status": "todo", "required": "L4", "validated": "L0" },
    "audio":         { "status": "in_progress", "required": "L2", "validated": "L1",
                       "note": "menu music in; gameplay + SFX pending" },
    "save_load":     { "status": "n/a", "required": "L3", "validated": "L0",
                       "reason": "single-session arcade" }
  }
}
```

`status` ∈ `todo|in_progress|done|n/a|blocked|stale`. Complete = `done && validated ≥ required &&
(feel ∈ {n/a,passed})`. `%complete` excludes `n/a`. **`stale`** is set by the regression sweep (§8).

**`GAMEPLAN.md`** — the human/agent GDD (the *why*). Sections: `Vision & Pitch` · `Genre / Demographic /
Tone` · `Core Loop` · `Story & Setting` · `World & Levels` · `Mechanics` (spec each *precisely* — vague
"make the door open" burns tokens; "door slides on +X" is cheap) · `UI / Screens` · `Audio Direction` ·
`Feel / Juice targets` · `Win / Lose & Difficulty` · `Art Direction` · `Accessibility & Localization` ·
`Milestone Notes` · `Known Workarounds` · `Open Questions`.

### 6.2 MCP surface — one rollup tool + one resource (tool-count-aware)

`production(op, …)`: `init{genres,stage}` · `status` · `set{milestone,status?,validated?,feel?,note?}` ·
`add_milestone`/`remove_milestone` · `validate{milestone}` (runs validator → depth + evidence, writes
back) · `report` (full completeness report for packaging & `/gamedev-status`) · `sweep` (regression, §8)
· `advance_stage` (checks Axis-A exit criteria). Read-only resource **`phyxel://production`** mirrors
`status` for cheap cold-session read (the Unity resource pattern).

### 6.3 Onboarding — SessionStart digest injection

Extend the hook: `phyxel up` also runs `phyxel status` (reads `production.json` + `GAMEPLAN.md focus`,
**no engine needed**), forwarded as injected context:

```
[phyxel] TestGame (platformer) — stage: vertical_slice — 6/14 milestones (43%)
  DONE:  design_brief, world, player, level_playability, main_menu, hud
  NEXT:  win_condition (todo, L4), audio (in_progress, L1→L2)
  ⚠ STALE: core_loop was done@L4, invalidated by edits 2026-07-12 → needs re-validate
  FOCUS: wiring the boss fight
  See GAMEPLAN.md; production(op="report") for the full ledger.
```

**Stale milestones surface at the top** — the digest must never report rotted "done" as current truth.

### 6.4 Packaging — soft gate + report

`package_game.py` reads `production.json`, computes the completeness report, and **WARNS** on any
incomplete / under-validated / **stale** required milestone but proceeds (prints the report). If
`strictPackaging:true`, those become blocking **errors**. Slots into the existing `errors[]`/`warnings[]`.

### 6.5 The verification loop (the #1 real-world enabler)

L3/L4 validation *is* the screenshot/PIE loop every practitioner cited: `launch_engine --project` → drive
the scenario → `screenshot`/`get_visual_diagnostic`/`get_render_stats` → auditor reads evidence → record.
This is where Phyxel is already ahead of the median MCP server — we make it the backbone of `op:"validate"`.

---

## 7. The interactive process (per-session lifecycle)

1. **Orient** — digest is already in context (§6.3); **stale milestones first**, then `next`.
2. **Regression check** — if new work touched a blast-radius, `production(op:"sweep")` before adding more.
3. **Pick** — user directive, else recommended `next` for the current stage.
4. **Design-first** — under-specified milestone → update `GAMEPLAN.md` (spec mechanics *precisely*) before building.
5. **Build** — via existing skills/MCP (`phyxel-world/-characters/-mechanics/-assets`).
6. **Validate red-before-green** — `op:"validate"`; L3/L4 via the §6.5 loop. **Auditor confirms L3+.**
7. **Feel pass** — for interactive milestones, run the juice-pass gate → set `feel`.
8. **Record + snapshot** — `op:"set"` writes status/validated/feel/`validatedAt`/`hash`; `create_snapshot`
   stamps the rollback point; update `focus`.
9. **Checkpoint** — at stage boundaries (esp. before `vertical_slice`→scale, before `content_complete`),
   surface to the user for a human go/no-go (autonomous multi-milestone runs are documented unreliable).
10. Repeat → `advance_stage` when Axis-A exit criteria met → eventually `package`.

**Creator ≠ certifier ≠ tester:** the building session builds; an **auditor** certifies L3+; a **fresh
non-builder session** runs the adversarial playtest (§9). Same discipline as the engine's Stop-hook gate.

---

## 8. Durability & regression (v2's biggest addition — the #1 gap)

The v1 tracker was **monotonic**: "done" was permanent, validation never expired, so a later edit could
silently rot a finished milestone and the digest would still report it green. Real practice treats a
completed unit as **revocable** and its validation as **perishable**.

- **Staleness / invalidation** — each `done` milestone carries `validatedAt` + a `hash` over its
  *validation inputs* (the game.json regions / files / triggers it depends on). When those change, the
  milestone flips to `stale`; the digest surfaces it; it must be re-validated to return to `done`.
- **Blast-radius sweep** — `production(op:"sweep")` re-runs prior milestones' red-before-green checks along
  the dependency graph of what was edited ("Agent A changed a shared util Agent B relied on" is the named
  #1 agent failure mode). Cheap for a render engine: geometry hashes, `get_render_stats`, deterministic
  checks, golden screenshots.
- **Snapshot per milestone** — completion stamps a `create_snapshot`; a detected regression can
  `restore_snapshot` (roll back), not merely report. Mirrors the "git-commit-per-milestone" the real
  builds all used.
- **Golden baselines** — per-milestone screenshot / geometry-hash / trigger-replay committed and reviewed
  like code; the sweep diffs against them. *Red-before-green proves a milestone entered `done`; the sweep
  proves it stays `done`.*

> Caveat learned from Claude Code's own checkpointing: local undo doesn't track MCP-driven world-DB /
> `production.json` mutations. Our hash/snapshot is engine-side precisely so **MCP mutations are covered**.

---

## 9. Completability & adversarial playtest

Per-milestone L0–L4 validates pieces **in isolation, by the agent that built them** — which shares the
build's blind spots (the SMART result: goal-pursuing agents hit ~55% coverage and miss defensive/illegal
branches). So an all-green game can be **unwinnable, softlocked, trivially easy, or exploitable**, and
`qa_pass` as a lone checkbox won't catch it. Two additions:

- **Whole-game completability check** — a reachability/softlock pass the builder can't self-satisfy: can
  the win state be reached from the start, and is it *still* reachable from representative mid-game states
  (no softlock)? This is `TraversalProbe` elevated from one structure to the whole level/game graph.
- **Adversarial playtest by a fresh non-builder session** — promote L4 from "one human checkpoint" to an
  **exploratory agent in a different context**, instrumented for coverage, prompted to *break* the game
  (out-of-bounds, sequence-break, resource exploits, illegal-state guards) — plus a **replay regression
  suite** (record traces, re-run each build, diff outcomes). Balance/`difficulty_balance` is a *play
  distribution*, measured here, not a build-time artifact.

Honest ceiling: LLM test agents find ~60–75% of human-found bugs — this augments the human playtest gate,
it doesn't replace it.

---

## 10. Target genres: survival · RPG/D&D · action-RPG (primary focus)

These are **systemic, content-heavy, persistence-critical** games. Two properties reshape how the whole
tracker applies to them.

### 10.1 Reframe — the mechanics already exist; validate integration, content, balance

Phyxel already ships the substrate these genres need (per CLAUDE.md — verify APIs against current code
before building on a specific one):

- **D&D / RPG layer:** DiceSystem · CharacterAttributes (6 abilities + modifiers) · Proficiency ·
  CharacterSheet/CharacterProgression (class/race/XP/level, data in `resources/rpg/`) ·
  ActionEconomy/InitiativeTracker · AttackResolver · ConditionSystem (15 conditions) ·
  SpellDefinition/SpellcasterComponent/SpellResolver · RpgItem/CurrencySystem/Attunement/Encumbrance ·
  ReputationSystem/DialogueSkillCheck/SocialInteractionResolver · RestSystem · WorldClock (360-day +
  lunar) · Party · LootTable · EncounterBuilder · CampaignJournal.
- **Survival-adjacent:** CraftingSystem (+ recipes) · HazardSystem · DayNightCycle · NPC
  needs/schedules/relationships/worldview · HealthComponent/RespawnSystem · EquipmentSystem (6 slots).

**So for these genres the tracker's job is mostly "prove these systems are wired into a balanced,
content-full, persistent, shippable game" — not "implement them."** Most genre milestones here are
*integration + content + balance* checks, which is a different and largely un-tooled problem than
primitive-implementation. Where a mechanic is missing, it's a normal build milestone; where it exists
(usually), the milestone is a **wiring + validation** task.

### 10.2 Genre milestone sets + starter scaffolds

Merged onto CORE (§4b) + cross-cutting (accessibility/localization). Phyxel systems in parens.

- **Survival:** `needs_model` (hunger/thirst/temperature/stamina/fatigue — NPC-needs → player) ·
  `gathering_crafting` (harvest→craft loop; Crafting+recipes) · `base_building` (shelter; structure gen)
  · `threat_escalation` (nights/waves harden over time; Hazard+spawns) · `resource_economy` (sources ⇄
  sinks — loop-closure §10.3) · `death_penalty_model` · `weather_exposure` (DayNight/weather as threat).
  Elevated `save_load` → save-integrity (§10.5). *Scaffold:* player + needs HUD + one craftable + a
  hostile night.
- **RPG / D&D:** `character_creation` (race/class/background/abilities/appearance flow + screen;
  CharacterSheet) · `quests` (multi-step, branching, quest log; ObjectiveTracker+StoryEngine) ·
  `dialogue` (branching + skill checks + consequence; Dialogue+DialogueSkillCheck) · `combat`
  (turn-based initiative; ActionEconomy/Initiative/AttackResolver/Conditions) · `itemization` (loot
  tables, rarity, equip stats; RpgItem/LootTable/Equipment) · `progression` (XP/level/feats/spells;
  CharacterProgression/Spell*) · `factions_reputation` (Reputation) · `economy_merchants` (Currency +
  buy/sell) · `party_companions` (Party) · `rest_camp` (RestSystem+WorldClock) · `living_world` (NPC
  schedules/relationships) · `campaign_structure` (arcs/encounters/journal;
  StoryEngine/EncounterBuilder/CampaignJournal) · `codex_journal` · optional `world_map_travel`.
  *Scaffold:* creation screen + one town + one NPC with a 2-step quest + one encounter.
- **Action-RPG:** the RPG set, but `combat` is **real-time** (hit reactions, i-frames, dodge/roll,
  lock-on targeting, telegraphs, combos — a heavy Axis-C *feel* milestone) + `boss_encounters` (phased)
  + `loot_build_diversity` + `enemy_scaling`. *Scaffold:* real-time controller + one enemy + one dodge +
  one loot drop.

> **Turn-based vs real-time combat is a genre *parameter* on the `combat` milestone, not a fork** —
> D&D-like → initiative; action-RPG → real-time. Both supported; a game may use both (e.g. tactical
> encounters + real-time exploration).

### 10.3 Systems-interaction matrix (the systemic-game centerpiece)

The signature failure of these genres: each system works alone, but the *combinations* — where the fun
and the bugs live — are never checked. No surveyed tool does this. Declare a **systems-interaction
matrix** in `GAMEPLAN.md` — "system A × system B → expected effect," each an **L3** scripted in-engine
scenario (`TraversalProbe` generalized from geometry to system pairs):

| A × B | Expected |
|---|---|
| rain × campfire | extinguishes |
| cold × shelter | warmth restored |
| hunger=0 × time | health drains |
| rest × enemy-nearby | rest blocked |
| encumbrance-over-limit × movement | speed penalty applies |
| reputation(faction) × merchant | prices / dialogue options change |
| condition(frozen) × fire-damage | shatter / bonus |
| poison × antidote | cured |
| quest-item × lost/consumed | quest still completable (or item re-obtainable) |

**Survival corollary — resource-loop-closure / no-dead-end:** every consumable the player *needs* must be
renewably obtainable from a reachable state. A required resource with a sink but no reachable source is a
survival *softlock* (the §9 completability check specialized to the resource economy). Validate the
resource graph: no sink without a matching source.

This is the highest-value genre-specific validator — and exactly where these games ship broken.

### 10.4 Content-volume manifests

"The quest system exists" (L4) ≠ "the game has enough quests." A one-instance system is a demo. Add
optional **count targets** on content milestones in `production.json` — e.g. `"quests": {"target": 20,
"current": 6}` — for quests / items / enemy types / recipes / dialogue nodes / biomes / spells. Phyxel is
data-driven (`resources/rpg/`, templates, recipes), so **`current` is countable automatically**.
`content_complete` (Axis A) gates on these counts, turning "content-complete" from vibes into a measured
bar. Counts are a floor, not the goal — pair with the balance pass (§10.6) so volume ≠ padding.

### 10.5 Save-integrity (deep persistence)

These genres break at save/load more than anywhere. A save must round-trip the **full state surface** —
character sheet, inventory/equipment, quest progress, NPC states/relationships/reputation, base builds,
world time/calendar, story variables, spawned entities. Elevate `save_load` to a **save-integrity**
validator: `save → reload → deep-diff every persisted subsystem == identical`. Phyxel's SQLite
`WorldStorage` + StoryEngine persistence + CharacterSheet are the substrate; this is a scripted
round-trip check, not a checkbox. (Ties to the "every DB-load path must call `buildAllChunkPhysics()`"
class of load-time bugs — a reload that drops a subsystem is exactly what this catches.)

### 10.6 Progression pacing & economy balance

The "3.9s vs 10–30s fun window" lesson is *acute* here: XP curves, gear power curves, resource-drain
rates, crafting costs, encounter difficulty (CR), and loot rarity are make-or-break — and they're **play
distributions, not artifacts**. Add a `progression_pacing` concern measured via simulated/played sessions
(time-to-level, power-vs-content curve, resource surplus/deficit over time, encounter win-rate spread),
run in the §9 adversarial/balance pass — never a static check. This is the genre-specific face of the
universal "tests pass ≠ fun" bottleneck.

---

## 11. Phased roadmap

Each phase ships independently.

0. **Data spine (no engine changes). ✅ BUILT (2026-07-12, uncommitted).** `production/v2` schema doc
   (`milestone-schema.md`), stages doc (`production-stages.md`), `GAMEPLAN.template.md` (incl. the
   interaction-matrix + content-manifest sections), genre-template JSONs (`core` + `survival` + `rpg` +
   `action-rpg`, §10) with interaction-matrix seeds + content targets + starter-scaffold notes, and two
   seed recipes. `phyxel new`/`link --genre <g>` now scaffold `.phyxel/production.json` (stage=`concept`,
   CORE + genre, override-merged) + a rendered `GAMEPLAN.md` — idempotent, best-effort (skips cleanly if
   the engine repo isn't resolvable). Verified: templates parse; survival→31 / survival+rpg→45 milestones
   with `save_load`→L4 & `combat`→real_time overrides; unknown-genre errors; files land beside the
   existing scaffold. **Fast-follows also done:** each genre template ships a real `starter` game.json
   (genre-appropriate world/player/camera, structurally valid) that `phyxel new --genre` merges over the
   flat default; recipe library grown to 5 (farming-survival-loop, rpg-quest-chain, save-integrity,
   turn-based-encounter, interaction-matrix-setup).
1. **Onboarding digest. ✅ BUILT (2026-07-12, uncommitted).** `phyxel_cli/status.py` (`phyxel status`)
   reads `production.json` → compact **stale-first** digest: `[phyxel] <name> (<genres>) - stage - done/
   total (pct%)` + DONE / NEXT (in_progress-first, natural order) / STALE / BLOCKED / ORDERING-CRITICAL-
   not-started nudge / FOCUS. Wired as a 2nd `SessionStart` hook command (after `phyxel up`) so every
   session is auto-oriented; ASCII-only (survives hook stdout / Windows consoles); silent in non-tracker
   dirs. No engine required. Verified against fresh + progressed trackers. *Value: the headline vision —
   every session knows what's done/next — zero engine code.*
2. **`production(op=…)` rollup tool. ✅ BUILT (2026-07-12).** One MCP tool `production` in the MCP
   server (`scripts/mcp/phyxel_mcp_server.py`) backed by a self-contained `scripts/mcp/production_tracker.py`
   — **pure file ops on `.phyxel/production.json`, no engine/`/api` routes** (the design's engine-route
   plan was over-built; production.json is just a project file). Ops: `status` (the digest), `report`
   (full ledger + incomplete-required), `set` (milestone status/validated/feel/note/reason/evidence +
   project focus/stage), `add_milestone`/`remove_milestone`, `advance_stage`. Validates inputs (status/
   L-level/feel enums; n/a needs a reason). Project resolved: explicit arg > `PHYXEL_PROJECT` env > the
   engine's loaded project. Removes the dogfood's JSON hand-editing. Verified: all ops + error paths on
   the real Emberwake tracker. *Acceptance owed: the tool loads when the MCP server (re)starts — a live
   session must reconnect to invoke it.* `phyxel://production` **resource deferred** (the SessionStart
   digest already covers cold-session orientation; the server declares no resources yet).
3. **Static validators (L1/L2) + `op:"validate"` + report.** Catches silent-drop ship bugs.
4. **Durability core (§8).** `validatedAt`+`hash`, `op:"sweep"`, snapshot-per-milestone, golden baselines.
   *Value: "done" stops rotting — the biggest structural fix.*
5. **Packaging soft-gate + `strictPackaging`.**
6. **Runtime validators (L3/L4) + feel-pass gate + auditor discipline + human checkpoints.**
7. **Completability + adversarial playtest (§9) + systemic-genre validators (§10)** —
   reachability/softlock + fresh-session exploratory playtest + replay regression + the
   **interaction-matrix**, **resource-loop-closure**, **save-integrity**, and **progression-pacing**
   checks (§10.3/10.5/10.6). Content-manifest counting (§10.4) is cheap and can land as early as Phase 3.
8. **Guided-process skill (`/gamedev-next`) + recipe library (§4d) + creator→auditor→tester sub-agent
   pairing.** Seed genre-primed recipes (farming-survival-loop, rpg-quest-chain, turn-based-encounter,
   real-time-combat, save-integrity…) so every session builds a given feature the same way.

**Recommended first slice: Phase 0 + 1** — artifact + auto-onboarding + a running starter game, no engine
C++, all in `tools/phyxel-cli` + data. Validate the schema on a real project before the engine-side work.
**Then Phase 4 (durability) early** — it's the highest-leverage structural piece and cheap for a render engine.

---

## 12. Open questions / risks

- **Milestone granularity per scene** — per-game with scene-scoped notes; revisit for big multi-scene titles.
- **GAMEPLAN ↔ production.json drift** — process (§7) updates both together; `op:"validate"` can flag
  "GAMEPLAN claims X but no artifact."
- **`n/a` honesty** — requires a `reason`; auditor challenges suspicious `n/a`s.
- **Hash scoping** — too broad → everything constantly `stale`; too narrow → misses real breakage. Start
  file/region-scoped, tune from false-positive rate.
- **Feel is subjective** — the juice-pass gate is a *presence* checklist (sound-on-input, easing, coyote
  time), not a "fun" judgment; genuine fun still needs the human playtest.
- **Automatic depth ceiling** — many milestones auto-validate only to L2; be honest which level was
  actually reached (never imply L4 from an L2 check).
- **Tool-count cliff (adjacent, §2d)** — this adds ~1 tool; don't let the feature grow the ~300 surface.
- **Interaction-matrix authorship** — who writes the matrix (§10.3)? Seed a default per genre template
  (rain×fire, hunger×time, encumbrance×movement…) so the agent extends rather than invents from scratch.
- **Combat model default** — turn-based (D&D) vs real-time (action-RPG) is a `combat` parameter; a game
  may want both (tactical encounters + real-time exploration). Confirm per project in `GAMEPLAN.md`.
- **Death/permadeath & New Game+** — the `death_penalty_model` (survival) vs respawn/load (RPG) choice
  ripples through save design and balance; decide early. NG+/replayability left optional.
- **Content-count honesty** — counts (§10.4) are a floor, not a goal; guard against padding-to-hit-target
  (auditor + balance pass judge quality, not just quantity).

---

## 13. File map (✅ = built in Phase 0)

```
docs/game-production/
  README.md                     <- this design plan (canonical entry)
  milestone-schema.md           <- production/v2 field reference (Phase 0) ✅
  production-stages.md          <- Axis-A gates + exit criteria (Phase 0) ✅
  GAMEPLAN.template.md          <- rendered into each project's GAMEPLAN.md (Phase 0) ✅
  genre-templates/*.json        <- core + survival + rpg + action-rpg (Phase 0) ✅
  recipes/*.md                  <- task-level build playbooks, genre-primed (Phase 0/8, §4d) ✅ (2 seeded)
  ValidationLedger.md           <- per-project generated
tools/phyxel-cli/phyxel_cli/
  production.py                 <- merge core+genre -> production.json + render GAMEPLAN (Phase 0) ✅
  scaffold.py                   <- link()/new() now scaffold the tracker; --genre (Phase 0) ✅
  status.py                     <- `phyxel status` digest reader, stale-first (Phase 1) ✅
tools/phyxel-gamedev/
  hooks/hooks.json              <- SessionStart also injects the digest (Phase 1) ✅
  skills/gamedev-next/          <- guided-process skill (Phase 8)
scripts/mcp/
  production_tracker.py         <- production.json ops (status/set/add/report/advance) (Phase 2) ✅
  phyxel_mcp_server.py          <- `production` MCP tool wired in (Phase 2) ✅
engine/  (Phases 3–7, engine-side)
  MilestoneValidator registry + completability/TraversalProbe-at-game-scale
  + /api/production/validate route (only the validators that must introspect the running game)
  + regression sweep
tools/package_game.py           <- completeness soft-gate + stale check (Phase 5)
```

---

### Provenance
Grounded in: internal infra map (`create_project.py` / `phyxel-cli` / `phyxel-gamedev` plugin /
`GameDefinitionLoader` / `package_game.py`); external survey of Godot/Unreal/Unity/Blender MCP servers,
consumer AI builders (Rosebud/Astrocade/SEELE/Summer/GDevelop/Buildbox/Ludus), pipelines (GameGPT /
gamestudio-subagents / StraySpark / RuleSmith), real studio practice (Tim Cain scope-vs-quality;
Supergiant content-lock), game-feel discipline (Swink; "Juice it or lose it"), automated-QA research
(Go-Explore, SMART coverage, softlock model-checking), and **four real weekend AI game builds** (Stefan
Vaskevich's UE 5.8 fox runner — the r/TopologyAI post + its transcript; "From C64 to Claude"; Chier Hu's
survey; HermeticOrmus patterns). Note: "Topology AI" is **not** a tool — r/TopologyAI is the community of
Top3D.ai, a 3D-generator leaderboard; the build used UE + Claude Code + UnrealClaude/VibeUE MCP. Sources
web-verified with uncertainty flagged (Reddit unfetchable; Epic/Unity-official specifics unconfirmed).
