# Standalone Parity Gaps — the editor-vs-shipped-game divergence ledger

> **Question this answers:** "could I build a full RPG (story, areas, NPCs, loading screens,
> menus, progression, combat, saves) start-to-finish today?" — **In the editor mostly yes; as a
> shipped standalone, no.** The gap is the generated-game scaffold (`tools/create_project.py`),
> not the engine: the mechanics exist as engine subsystems but most are never instantiated in
> the game a player runs.
>
> **Provenance:** static analysis (2026-08-12, solution-auditor PASS) + runtime measurement on a
> real packaged Release build (2026-08-13, RpgGapProbe, driven via the `--test` GameApiService).
> Raw evidence: [`docs/evidence/rpg-gap-probe/`](../evidence/rpg-gap-probe/) — probe script,
> game definition, raw HTTP-step evidence JSON, full game log.
> **Context:** extends `README.md` §6.6, which lists 4 divergence rows; the measured divergence
> is larger (§1). File found-items via `/feedback` → `/triage-feedback` as they get fixed.

## 1. Subsystems present in the editor host but ABSENT from the generated standalone

Verified by symbol search over `tools/create_project.py` + `engine/include/core/GameShell.h` +
`engine/include/core/EngineRuntime.h` (zero hits each), including transitive reachability
through `GameApiService.h`/`GameCallbacks.h` (auditor-checked). Editor wiring cited for contrast.

| Subsystem | Editor | Standalone | Consequence for an RPG |
|---|---|---|---|
| `ObjectiveTracker` | `Application.cpp:6448` | absent | no quest log / objective HUD; `complete_objective` unhandled (§4 M1) |
| `Inventory` | `Application.cpp:1540` | absent | no inventory screen or state |
| `PlayerProfile` save/load | `Application.cpp:6553-6591` | absent | **no persistence of any kind in a shipped game** |
| `CombatDirector` (turn-based) | `Application.cpp:555-1168` | absent | the verified BG3-style combat does not exist in a shipped game |
| `CharacterSheet` / `CharacterProgression` | via NPC/combat paths | absent | no stats, XP, or levels |
| `SceneCallbacks::setLoadingScreen` | not wired either | not wired | loading screens render nothing (§2, §4 M2) |
| `AudioSystem` | wired + driven | **inherited but unused** — `EngineRuntime` owns it (`EngineRuntime.cpp:73`) but the scaffold never calls `getAudioSystem()`; settings sliders mutate dead fields (`create_project.py:1140-1165`) | silent game; inert volume sliders |

What the scaffold DOES wire (and §4 confirms works at runtime): SceneManager multi-scene,
NPCManager + dialogue, StoryEngine, TriggerSystem, GameScreen shell (intro/menu/victory/credits),
UISystem HUD/menus, input, per-scene world self-baking.

## 2. Loading screens: the default transition style renders nothing

- `SceneTransitionStyle::LoadingScreen` is the **default** (`SceneDefinition.h:53`).
- `SceneManager` invokes `callbacks_.setLoadingScreen(...)` at both transition phases
  (`SceneManager.cpp:100,112,126,137`) — null-guarded, so an unwired callback silently skips.
- The ONLY assignment in the repo is `tests/integration/SceneIntegrationTest.cpp:86`. Neither
  the editor nor the generated game wires it (generated `SceneCallbacks` sets
  clearEntities/clearNPCs/endDialogue/onMenuSceneLoaded/onSceneReady — nothing else).
- `UI::ScreenState::Loading` exists (`GameScreen.h:18`) and nothing ever enters it (§4 M2).

## 3. Further authoring/pipeline gaps (static analysis)

- **Trigger action vocabulary diverges.** Standalone executor handles 5 actions
  (`transition_scene, quit_game, show_victory, show_credits, set_story_variable`) and
  LOG_WARNs the rest (`create_project.py:732-761`); the editor additionally handles
  `complete_objective`/`fail_objective` (`Application.cpp:511-513`). **A game.json authored
  and tested in the editor silently changes behavior when packaged.**
- **Objectives are not authorable.** `"objectives"` is not a `game.json` key (no hits in
  `GameDefinitionLoader.cpp`); `GameSubsystems` has no ObjectiveTracker member. Objectives
  exist only via MCP calls against the editor at runtime.
- **Multi-scene world-start games skip the main menu** (runtime-confirmed, §4 M5). The
  generated shell enters Intro/MainMenu only when the start scene is not a world scene.
  A shipped game gets a main menu only if the author adds a `sceneType:"menu"` start scene.
- **No Quest layer.** No Quest/QuestSystem type in `engine/`. `ObjectiveTracker` is flat
  (id/title/status/category/priority) — no stages, prerequisites, rewards, giver, turn-in.
- **Progression is an orphan library.** `CharacterProgression::awardXP` has zero callers
  outside its own translation unit and unit test. No character-creation flow exists anywhere.
- **PlayerProfile persists 5 things** (camera pose, health, spawn, deathCount, inventory
  blob). Not persisted: character sheet, XP/level, equipment, objectives/quests, story
  variables, NPC state, world clock. The §10.5 save-integrity validator is not built, and
  the state surface it would validate mostly isn't persisted yet.
- **Content volume** (counted 2026-08-12): 12 classes, 10 races, 4 spells (+1 anim-map file),
  19 monsters, 1 loot table, 4 items (+1 anim-map), 1 faction, 2 dialogue files.
  `resources/rpg/` cited in CLAUDE.md does not exist — data lives in
  `resources/{classes,races,spells,monsters,loot_tables,rpg_items,factions,dialogues}`.
- ~~Scaffold nit: project asset dirs not created~~ **RETRACTED 2026-08-13** —
  `create_project.py:316-318` does create `worlds/`, `shaders/`, `resources/textures/`;
  the original observation came from a file-only directory listing that hides empty dirs.

## 4. Runtime measurements — RpgGapProbe (packaged Release build, 2026-08-13)

Probe: a 2-scene game (`town` ⇄ `cellar`, both `transitionStyle:"loading_screen"`, Flat 3×3-chunk
worlds, 1 dialogue NPC, per-scene triggers) generated by `create_project.py`, compiled Release
against `phyxel_core` @ `337b8cc0`, run as `RpgGapProbe.exe --test 8100`, driven over HTTP.
Evidence files: [`gap_probe_evidence.json`](../evidence/rpg-gap-probe/gap_probe_evidence.json)
(raw step responses) + [`rpggapprobe_game.log`](../evidence/rpg-gap-probe/rpggapprobe_game.log).
(A first run's M2 polled only one transition — flagged by the solution-auditor; the archived
evidence is the corrected rerun with both transitions polled identically.)

| # | Prediction (static) | Runtime result |
|---|---|---|
| M1 | `complete_objective` unhandled in the shipped executor | **CONFIRMED** — `[10:49:15.012] [WARN] [RpgGapProbe] Unhandled trigger action 'complete_objective' (trigger 'win_quest')` while the same trigger's `show_victory` executed |
| M2 | `loading_screen` transition renders no loading screen | **CONFIRMED** — screen state polled at ~30 ms for 8 s through EACH of the two transitions (`transition_states_to_cellar`, `transition_states_back_to_town`): `loading` never observed (both sequences `["playing"]`). A sub-30 ms flash is not excluded by polling alone, but is by construction: `setLoadingScreen` is never assigned in the generated `SceneCallbacks` (RpgGapProbe.cpp), so `ScreenState::Loading` is structurally unreachable |
| M3 | (control) `show_victory` works in the real shell | **CONFIRMED** — screen → `victory` (then `credits` on dismiss); proves the probe observes the real state machine, and that the shell screens themselves work |
| M4 | per-scene triggers load and replace per scene | **CONFIRMED** — town lists `win_quest`+`to_cellar`; after transition, cellar lists only `back_to_town` |
| M5 | world-start multi-scene skips intro/main-menu | **CONFIRMED** — first screen query returned `playing`; intro/menu never appeared |

Also measured (positives worth keeping):
- **Scene round-trip works in the shipped build**: town→cellar→town, ~180 ms each, no crash;
  NPC restored on re-entry; re-entry position restored. (The editor's intermittent
  `vulkan-1.dll` transition crash did not reproduce here — tiny worlds, N=1.)
- **The standalone self-bakes missing scene DBs** from inline `world` blocks
  (`worlds/town.db` + `worlds/cellar.db` created at first load) — pre-baking is an
  optimization, not a requirement, for small worlds.
- API/game stayed responsive throughout; player + NPC entities live with UUIDs.

Two engine bugs surfaced by the log (unfiled as of this writing):
- `SceneManager` logs `{:.1f}` format specs literally (`Scene 'town' loaded in {:.1f}ms`) —
  the logger doesn't support that syntax (same class as the `BitmapFont::initializeTTF` item
  in `HudSystem.md` §6).
- The re-entry log line's arguments misalign: `Restored re-entry position ({:.1f}, {:.1f},
  {:.1f}) for scene '16'` — the scene-id slot consumed the x-coordinate.

**Not measured:** menus-scene flow (no `sceneType:"menu"` scene in the probe), HUD/dialogue
rendering (no screenshot path on the standalone API), audio at runtime, editor-side behavior
(no editor run), anything at world scale.

## 5. Fix round 1 — scaffold parity SHIPPED + green run (2026-08-13)

The §5→1 scaffold-parity fixes landed in `tools/create_project.py` (+ a new
`resources/ui/loading_screen.json`, packaged via `package_game.py` REQUIRED_RESOURCES) and
were measured green on a regenerated + rebuilt RpgGapProbe (same phyxel_core, only the
generated game recompiled). Evidence: `green_probe.py`, `green_probe_evidence.json`,
`green_runA.log`, `green_runB.log`, `game_definition_green.json` in
[`docs/evidence/rpg-gap-probe/`](../evidence/rpg-gap-probe/).

| Red (§4) | Fix | Green measurement (run A/B logs + HTTP evidence) |
|---|---|---|
| M1 `Unhandled trigger action 'complete_objective'` | ObjectiveTracker member + editor-parity `complete_objective`/`fail_objective` actions + `objective_complete` event re-emission; NEW: top-level `game.json "objectives"` array is loaded (`Loaded 1 objective(s) from game.json`) | zero `Unhandled trigger` lines; `Trigger 'quest_chain' fired (when: objective_complete)` — a declarative quest chain composes end-to-end; `quest_chain.fired == true` over HTTP |
| M2 no loading screen | `cb.setLoadingScreen` wired → `ScreenState::Loading` → data-driven `loading:*` overlay (`loading_screen.json`, `{{loading_target}}` = scene name) | `Loading screen shown (-> 'Cellar')` / `dismissed` on BOTH transitions **and at boot** (1.8 s world gen); the same 30 ms poll that saw only `["playing"]` in the red run now sees `["loading","playing"]` in both transitions |
| no persistence | `PlayerProfile` member; load-at-boot after scene load; save on the new `save_game` trigger action AND at quit (`onShutdown`) | run A: `Player profile saved` ×2, clean exit 0; `player_state` row in `town.db` (sqlite-verified); run B: `Restored saved player profile`, and the boot camera is the SAVED pose (16, 18.9, 16), not the authored definition camera (28, 24, 28) |
| inert volume sliders | `applyAudioSettings()` → `AudioSystem::setChannelVolume` (Master/Music/SFX) at boot + on every slider change | **code-wired + compiled + executed, NOT audibly verified** — the probe plays no sound; honest depth: L2 |

**Round 1b — test-mode cursor grab (user-reported, 2026-08-13):** a `--test` game grabbed the
real OS mouse (`GLFW_CURSOR_DISABLED` on entering Playing) and a hard-killed process left the
`ClipCursor` rect **permanently confining the cursor** — measured: red run clipped to
`(346,369,1626,1089)` during play AND after `proc.kill()`, with the user's browser foreground.
Fix (both in `create_project.py`): `updateCursorMode` early-outs on `config.testApiEnabled`
(set in `main()` before init — `testApiRunning()` is false during boot, so it can't be the
guard), and `onShutdown` releases the cursor before teardown for normal quits.
**Measured (fix 1 only):** green run, identical conditions (api up, screen=playing, window
found): never clipped at any sample, including after a hard kill. Evidence: `cursor_probe.py`
+ `cursor_probe_evidence.json`. **NOT measured (fix 2):** the `onShutdown` release is
code-reviewed only — the probe always runs `--test` (fix 1 means no grab ever exists to
release) and always hard-kills (onShutdown may not run); its target scenario, a graceful quit
of a NON-test game from Playing, has no runtime evidence (auditor finding, 2026-08-13). Its
red/green test: run the exe WITHOUT `--test`, confirm the clip engages while Playing, fire a
graceful quit, confirm the clip clears. **Known residual:** a non-test shipped game that
CRASHES or is force-killed/Alt-F4'd while Playing still orphans the ClipCursor (onShutdown
doesn't run on those paths) — the same failure class as the original report; a robust fix
needs an engine-level approach (e.g. clip-on-focus-only or a watchdog), not shell code.

Notes / residue from this round:
- `save_game` is currently a **standalone-only** trigger action (the editor's save path is
  the `save_player_state` MCP command) — a small reverse divergence; add it to the editor's
  executor for full two-way parity.
- `PlayerProfile` still persists only its 5 fields — the player CHARACTER's position is not
  among them (the camera pose is). The restore is real but thin; the §6 fix list's save-format
  item stands.
- Loading-screen coverage: single-scene games don't take this path (no SceneManager
  manifest); menu-scene transitions suppress the overlay by design.
- Still absent from the standalone, unchanged: Inventory, CombatDirector, CharacterSheet/
  Progression (§1 rows stand).

## 6. Ordered fix list

1. ~~**Scaffold parity**~~ **DONE 2026-08-13 (§5)** — ObjectiveTracker + PlayerProfile +
   AudioSystem hookup + `setLoadingScreen` + trigger-vocabulary parity, measured green
   against the §4 red baseline.
2. **Vertical slice as forcing function**: one town, one dungeon, one quest chain, character
   creation, save/reload — packaged and *played* (the `vertical_slice` gate of README §4a,
   run against the shipped build via `--test`).
3. **Wire progression**: combat kill / quest completion → `awardXP` → level-up → character
   sheet screen (pulls the UISystem scrollable-container widget forward).
4. **Quest layer** above ObjectiveTracker (stages, prerequisites, rewards, giver/turn-in),
   authorable in `game.json`; then the save format for it.
5. **Save-integrity deep-diff validator** (README §10.5) once PlayerProfile covers the real
   state surface.
