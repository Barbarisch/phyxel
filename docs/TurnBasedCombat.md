# Turn-Based Combat — Design (BG3-feel)

> Status: **S1–S7 done + committed (+ player spellcasting); a playable, BG3-shaped turn-based
> fight — click-to-move/attack, hit-chance, spells, tactical camera — is verified live.**
> Goal: a turn-based combat mode that looks and feels like Baldur's Gate 3, built on the
> existing headless D&D mechanics and the real-time character FSM. The engine supports **both**
> real-time and turn-based combat.
>
> Progress (branch `feature/turn-based-combat`):
> - **S1 CombatDirector** — done (`ee71aa7`), 10 unit tests.
> - **S2 Damage unification** — done (`e1d36f5`), +7 tests; `CombatSystem::applyDamage` funnel +
>   player/HUD single health store.
> - **S3 TurnActor** — done (`f0e9135`), 14 tests; headless turn-execution bridge + feet↔unit
>   constant (0.3048).
> - **S4 enemy turn AI** — done (`2625a0f`), **verified live**: enemy walks in (movement
>   debited), d20-vs-AC attacks, damage via the funnel (no double-dip), turns advance with the
>   COMBAT HUD. Adds `CharacterTurnBody` adapter + `combat/set_mode` dev endpoint.
> - **S5 player tactical control** — done (`1725420`), 6 tests, **verified live**: a full round
>   cycles (player action bar → attack via intent → End Turn → enemy AI turn → back to player,
>   both HP tracked). `PlayerTurnController` + `combat/player_move|player_attack|end_turn` HTTP
>   + action-bar UI + real-time-control suppression.
> - **S6 targeting & hit-chance** — core done (`d900488`), +5 tests, **verified live**:
>   `AttackResolver::hitChance`, targeting queries, `resolveCombatPick(ray)` (enemy→attack /
>   ground→move) shared by live LMB + `combat/player_pick`, `combat/targeting_info`, action-bar
>   hit-chance readout.
> - **Player spellcasting** — done, **verified live**: `PlayerTurnController::castSpell`
>   spends the action, resolves AttackRoll / SavingThrow / AutoHit / heal through the funnel at
>   the cast release frame, plays the cast animation + VFX (`Application::playCastVisual`),
>   `combat/player_cast` HTTP. magic_missile / fire_bolt / cure_wounds confirmed in-engine.
> - **AoE spells** — done, +1 test, **verified live**: `SpellDefinition` area model (Sphere exact;
>   Cube/Cone/Line bounded), `castSpell` applies full/half/0 per enemy in radius (5e fireball
>   semantics), `aoeTargetsAt`/`combat/aoe_preview`. Fireball on a goblin cluster hit 2-in-radius
>   (full 35 / save-half 17) and spared the distant one.
>
> **Integration milestone reached:** the S1–S6 loop is a playable turn-based fight (player +
> enemy taking real animated turns, click-to-move/attack with a hit-chance readout). Remaining
> subsystems are presentation/depth.
>
> **Follow-ups found:** (a) the player-factory `createAnimatedCharacter` binds the shared
> `playerHealth` to EVERY animated character — fine for the single player, but non-player
> animated characters (and the debug NPC) must not inherit it; real enemies are `NPCEntity`
> (own health). (b) `CombatBehavior`'s real-time `onHitFrame` is not yet mode-gated (only the
> player's is) — irrelevant while CombatBehavior isn't ticked in turn-based mode, but gate it
> when both can coexist.
>
> Decisions locked with the user (2026-06-17):
> - **Per-game global mode** — `game.json` picks the ruleset for the whole session; no
>   mid-game snap-in/out. (The encounter API keeps a mode-override door open for a future
>   "this one fight is turn-based" case, but it is unused for now.)
> - **Systems-first build** — each subsystem below is built to completion with headless/unit
>   coverage where possible, then one integration milestone assembles a playable fight.
> - **BG3-hybrid camera** — over-shoulder by default, free pull-back/rotate toward overhead
>   during a turn, built on the existing `CameraRig`s.

## 1. Motivation — what exists and what's missing

Two halves already exist and have never been connected:

**Turn-based mechanics (headless, unit-tested, operate on string entity IDs):**

| Piece | State |
|-------|-------|
| `Core::InitiativeTracker` | Turn order, rounds, surprise, reactions, per-participant `ActionBudget`. |
| `Core::ActionEconomy` (`ActionBudget`) | Action / bonus action / reaction / movement (feet) / free object; dash/dodge helpers. |
| `Core::AttackResolver` | Attack rolls, advantage/disadvantage, damage. |
| `Core::ConditionSystem` | 15 D&D conditions. |
| `Core::SpellResolver` / `SpellcasterComponent` | Spell resolution + slots. |
| `Core::CombatAISystem` | Drives enemy turns (pick target → move → attack → `endTurn`), but **execution is placeholder** (`setMoveVelocity` + instant attack, no animation). |
| `Core::EncounterBuilder` | Encounter assembly. |
| HTTP/MCP | `combat/state|start|next_turn|end|set_initiative`, plus `attack`, `cast_spell`. |
| `ImGuiRenderer::renderCombatHUD` | COMBAT banner + initiative-order panel (basic). |

**Real-time combat (the souls slice — the execution layer BG3 needs):**

| Piece | State |
|-------|-------|
| `Scene::AnimatedVoxelCharacter` FSM | Full melee / dodge / death / KO / hit-react states; `setControlInput`, `setFacingYaw`. |
| `Core::CombatSystem` | Hand-origin hit detection, damage, i-frames, `onHitFrame` wiring. |
| `Scene::CombatBehavior` | Real-time AI that drives a character via `setControlInput` so the **real FSM ticks**. |

**The gap:** the turn-based layer computes *who acts and whether they hit*, but it does not
move/animate live characters, gives the player no tactical input, and has no
movement-range / hit-chance / AoE presentation or tactical camera. There are also **two
damage paths** (real-time `CombatSystem → Application::playerHealth` vs turn-based
`AttackResolver → HealthComponent`) — the known "two player-health stores" bug. Turn-based
combat forces us to unify them.

## 2. Core principle — one execution layer, gated

Both modes drive the **same `AnimatedVoxelCharacter` FSM**. Turn-based never bypasses it; it
only **gates** when a character may issue FSM commands and **debits** the `ActionBudget`.
Only `CombatDirector` answers "in combat / whose turn / what mode" — no parallel flags in UI,
AI, or input. This is the single-source-of-truth rule applied to combat.

## 3. Subsystems (dependency-ordered)

### S1 — `CombatDirector` (orchestration, single source of truth) — *in progress*
New `Core::CombatDirector` owns: active `CombatMode`, the `InitiativeTracker`, the
participant↔Entity map, and combat lifecycle (`beginEncounter(entities)` / `endEncounter`,
`advanceTurn`, queries). Replaces the loose `m_rpgInitiative` / `m_combatAI` wiring in
`Application`. The combat HTTP handlers and the combat HUD read from it. Mode is resolved
once from `game.json combat.mode` (default `real_time`). Headless-testable; no rendering or
FSM dependency in the type itself (it operates on entity IDs + a small entity-query
interface), so it stays unit-testable like the systems it wraps.

### S2 — Damage unification (prerequisite; fixes a known bug)
Collapse the two damage paths into one `applyDamage(target, amount, source, type)` entry that
both modes call; death/KO routes through the existing `die()` states. Resolves the dual
player-health stores.

### S3 — Turn execution bridge (`TurnActor`)
Translates a combat *intent* ("move to P", "attack T", "cast S") into FSM commands gated by
`ActionBudget`. Movement debits `movementRemaining` via a **single feet↔world-unit constant**;
attacks debit the action and resolve on the **animation hit-frame** (reuse `onHitFrame` →
`CombatSystem`), then signal turn-can-advance. This is what makes turns *animate* instead of
teleport, and it owns the turn-advancement-vs-animation-timing handshake.

### S4 — Enemy turn AI  ✅ done + verified live
`CombatAISystem` is now a per-turn phase machine (Thinking → Moving → Attacking → Done) that
runs an enemy turn through `TurnActor` + `CharacterTurnBody` over multiple frames: real
walk/attack animation, budget-gated movement, D&D d20-vs-AC to-hit, damage through the S2
funnel. Gated to turn-based mode via `CombatDirector`. (Remaining: richer tactical scoring —
weakest/most-dangerous target, multi-attack, AoE when clustered, death/flee handling.)

### S5 — Player turn controller (core BG3 input feel)  ✅ done + verified live
`PlayerTurnController` binds a `TurnActor` to the player on their turn and executes
move/attack/end-turn intents (movement debits the budget; attacks spend the action and
resolve d20-vs-AC through the S2 funnel). Action bar (Action / Bonus / Movement / **End
Turn**) in `renderCombatHUD`; real-time WASD/LMB suppressed during the player's turn. Intents
are HTTP/test-driven (`combat/player_move|player_attack|end_turn`). **Remaining for S6:**
click-to-move + click-target picking (raycast), ability/spell selection.

### S6 — Targeting & resolution preview  ✅ done + verified live (`d900488` + AoE)
`AttackResolver::hitChance` (pure; nat-20/1 floors + advantage/disadvantage), PlayerTurnController
targeting queries (`hitChanceVs`/`targetAC`/`distanceTo`/`inReachOf`/selected-target),
`Application::resolveCombatPick(ray)` (enemy-AABB → attack / ground-plane → move) shared by the
live LMB (cursor ray) and HTTP (`combat/player_pick`), `combat/targeting_info`/`select_target`,
the action-bar hit-chance readout, AND AoE templates (`aoeTargetsAt`/`combat/aoe_preview`,
affected-target preview). **Remaining:** advantage/disadvantage sourcing from ConditionSystem;
ground-point AoE targeting; on-ground movement-range ring (S8 presentation).

### S7 — Combat camera (BG3 hybrid)  ✅ done + verified live
`Application::updateCombatCamera` frames the active combatant (auto-pans on turn change),
pulls back to a tactical distance on combat entry, and orbits (RMB) + zooms (scroll) via the
third_person `CameraRig` — no character-movement input fed. Replaces the cameraCtl path for the
whole turn-based encounter. (Polish later: smooth pan interpolation, overhead/isometric toggle,
encounter-wide framing.)

### S8 — Combat presentation / UI  ⛔ BLOCKED on a Game-HUD system design
**The combat HUD built so far (`renderCombatHUD`: COMBAT banner, initiative panel, action bar,
hit-chance readout) is a VERIFICATION STOPGAP, not the intended design.** It's raw ImGui drawn
in the EDITOR's render path, so it overlaps the editor dev panels (World Outliner / Properties /
Viewport) and would not ship correctly — a shipped game needs its OWN HUD in the game-facing UI
layer (`UI::DialogueSystem`, `GameMenuRenderer`, `ScreenState`, `UI::renderCountdownHud`,
`Core::GameShell`), not editor ImGui.

Before doing "S8 polish" (portraits, dice/damage floaters, ground range/path), the engine needs
a proper **Game HUD system** — its own design session. Scope: (1) editor-vs-game rendering split
(play/game view: HUD overlays the scene, editor chrome hidden — preview what ships); (2) authoring
model (data-driven composable HUD definitions + a code/scripting escape hatch); (3) rendering
backend (reuse ImGui vs a retained game-UI renderer); (4) uniform data binding to live game state;
(5) a DEFAULT HUD module set + theming for rich dev customization. The combat HUD then becomes the
first default HUD module expressed in that system. Until then, do NOT add more game-HUD elements to
the editor ImGui.

### S9 — Reactions / opportunity attacks
`InitiativeTracker` already models reactions; trigger OAs when a creature leaves melee reach,
with prompt/auto-resolve.

### S10 — Conditions surfacing
Surface `ConditionSystem` conditions in the HUD; have them gate actions (prone/restrained →
movement/attack effects).

### S11 — Encounter authoring + test surface
`game.json` encounter defs via `EncounterBuilder`, trigger-driven combat start, MCP
extensions for headless testing (`move_on_turn`, `get_hit_chance`, AoE preview) atop the
existing combat tools.

### S12 — Voxel-native tactical depth (stretch)
High-ground / cover bonuses, shove/throw, jump-as-movement, and BG3-signature surfaces
(fire/water/grease) via the water + hazard + VFX systems.

## 4. Integration milestone
After S1–S5 + S8 are individually built/tested, assemble **one fight** (player + 1 enemy:
initiative → move → attack → end turn → enemy animates its turn → death) and verify by
running the engine. S6 / S7 / S9–S12 layer on after the loop is proven.

## 5. Risks to design around now
- **Feet↔world units** — one conversion constant, one place (movement range, reach, AoE
  radius all depend on it).
- **Turn advancement vs animation timing** — a turn must wait for the hit-frame / anim to
  finish before resolving and advancing; `TurnActor` owns this handshake.
- **Single combat-state source** — only `CombatDirector` answers "in combat / whose turn";
  UI, AI, and input read from it.
</content>
</invoke>
