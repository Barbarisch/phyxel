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

## 6. Vertical slice increment 1 — Hearthvale PLAYED THROUGH (2026-08-13)

The §7 fix-list's item 2 forcing function ran: **Hearthvale** (`PhyxelProjects/Hearthvale`,
3 scenes: menu → town ⇄ cellar) was authored in game.json, generated, compiled Release, and
**played start-to-victory over the `--test` API** — menu Begin click → Elder dialogue accept
(E + choice) → walked east by steered `inject_input` → `to_cellar` region trigger + loading
screen → walked to the remedy (`find_remedy` fired + save) → walked out → town → dialogue
turn-in (choice → node `actions` → `complete_objective`) → `win` trigger → save + real
victory screen. First shipped Phyxel game completed by actual play. Evidence:
[`docs/evidence/hearthvale/`](../evidence/hearthvale/) (probe, evidence JSON, 3 run logs,
game definition).

Findings (each with its red proof in the evidence):
1. ~~Dialogue conditions missing~~ **RESOLVED same day (d4208fee) — authoring gap, not
   engine gap.** `"condition": {"variable": "...", "equals": ...}` on a choice works
   end-to-end in a shipped build (fail-closed on missing variables); pair with a
   `set_story_variable` trigger action. Red→green on the same probe: run C (immediate
   turn-in) can no longer reach victory; run A (legitimate path) still does. Document
   the schema in GameCreationGuide.
2. **Root-relative anim paths don't ship** — `animFile:"character.anim"` fails in a packaged
   build (editor-repo file, not in packaged `resources/animated_characters/`); omit for the
   humanoid default or the packager must map them.
3. **Authored scene camera not applied on menu→world transition** (camera stays engine
   default (50,50,50)); gameplay is unaffected (rig follows the player) — needs triage.
4. **`inject_input` key names are case-sensitive** (`Core::stringToKey`: `W`, `Enter`,
   `Escape`; lowercase silently unresolved) and unknown param shapes (`key` vs `keys`)
   return success — normalize + error on unknown params.
5. **Region-door authoring trap**: scene re-entry restores the player inside the departure
   region — `once:false` door pairs revolving-door loop. Authoring rule (or engine grace
   period) needed; slice used `once:true`.
6. Run B's save/reload measurement is incomplete (relaunch stops at the menu; profile
   applies on world-scene load) — extend the probe to click Begin before checking.
7. Pre-existing reflection-descriptor-pool Vulkan error also fires in standalones (known).

## 6b. Increment 2 — TURN-BASED COMBAT in the shipped game (2026-08-13)

The §1 CombatDirector row is closed: the generated standalone now wires the full editor
combat stack (CombatDirector + CombatAISystem + PlayerTurnController + Party + CombatSystem
damage funnel + CharacterTurnBody provider; per-frame ticks; turn-body map cleared on scene
transitions). Three new authoring surfaces: game.json `"combat": {"mode": "turn_based"}`,
the **`start_combat` trigger action** (encounters are authorable data), and 9 `combat/*`
commands on the test API (`/api/rpg/combat/*` bounced through the command queue onto the
game thread — no editor-style intent mutex needed). Engine: GameShell `apiCombat*` hooks,
GameApiService combat handlers + rpg-handler bounce, `EngineAPIServer::queueAndWait` public.

**Red→green, measured on Hearthvale:** red = `POST /api/rpg/combat/state` → **503** on the
pre-combat exe. Green (`hv_combat_probe.py` + `hv_combat_evidence.json` + `hv_combat.log`):
- C1 `combat/state` answers, `mode: turn_based` from game.json
- C2 walking into the cellar guard region fires the AUTHORED `start_combat` trigger —
  `Combat encounter started: 2 combatants (trigger 'guard_post')`, initiative rolled,
  player first
- C3 real D&D turns over the API: targeting (attack bonus 5 vs AC 14, 60% hit chance,
  distance 11.6 closed to 1.46 by `player_move`, then `in_reach` → attack, hit-frame
  resolution) — and the enemy AI took its own turn: `NPC 'npc_Rat' misses 'player'
  (roll 4 vs AC 14)`. Rounds advanced 1→4.
- C4 the quest still completes with the combat beat in the path (remedy → turn-in →
  victory screen).

**Increment 3 — the kill loop (same day):** NPC `maxHealth` was ALREADY authorable
(`GameDefinitionLoader.cpp:887` — never exercised in a shipped game). The scaffold's new
`onDamage` callback adds: death animation, **`entity_died` / `player_died` /
`combat_victory` trigger events**, and self-resolving encounters (dead combatant leaves
the initiative; last enemy down → encounter ends). Measured on Hearthvale (rat at
maxHealth 10): 3 landed attacks across rounds 1-4 → log sequence `Trigger 'rat_slain'
fired (when: entity_died)` → `Encounter won — last enemy fell ('npc_Rat')` →
`in_combat: false` with the probe's manual `combat/end` REMOVED → quest completes to
victory. A kill is now a first-class authorable event in shipped games.

**Increment 4 — PROGRESSION (same day):** `CharacterProgression::awardXP` — zero gameplay
callers when this ledger opened — now levels a real `CharacterSheet` in shipped games.
Scaffold: kills grant `progression.kill_xp`, objective completions grant
`progression.objective_xp` (both authorable, with class/race), level-ups log + fire a
`player_level_up` trigger event, and restore RE-LEVELS the sheet (average HP) rather than
poking numbers. Engine: `PlayerProfile` gained `xp`/`level` (the §5 save-format item),
`/api/rpg/sheet` returns the live sheet (`apiPlayerSheet` hook). Measured on Hearthvale
(fighter, 100 XP per kill/objective): rat + 2 objectives = 300 XP = **level 2 exactly at
the 5e threshold, landing on the final turn-in**; `/api/rpg/sheet` shows xp 300 / fighter 2
at the victory screen AND after relaunch. The relaunch initially failed (xp 0) — root
cause: profile restore ran only at BOOT, before a menu-start game opens its world DB;
moved to first-world-scene `onSceneReady`, once per session (re-entering a scene must
never rewind live progress). That same fix closes §6 finding 6 (run-B restore).

**Increment 5 — LOOT → INVENTORY (2026-08-14):** the last big §1 subsystem row closes. The
generated standalone owns a real `Core::Inventory`; **`give_item` / `remove_item` trigger
actions** make loot authorable data (dialogue node actions share the vocabulary, so an NPC
can take an item at turn-in); the inventory blob rides `PlayerProfile.inventoryData` through
save/load (the field existed from day one — nothing in a shipped game ever wrote it);
`/api/rpg/inventory` returns live state (`apiInventory` hook). Measured on Hearthvale: the
fetch quest is now LITERAL — `Item received: moonpetal_remedy x1 (trigger 'find_remedy')` →
hotbar slot 0 shows the item → the Elder's turn-in dialogue `remove_item`s it → zero items
after turn-in, with XP/level and relaunch restore intact from increment 4.

**Still deferred:** combat/objectives/hotbar HUD verification in the standalone (panels
exist; provider wiring for hotbar display unverified), findings 2-5 (anim paths,
menu→world camera, inject case-sensitivity, region-door grace), content volume,
RpgGapProbe regen on the current scaffold.

## 7. Ordered fix list

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

## 6c. Increment 6 � BG3 cameras (2026-08-14)

Third-person exploration + tactical birds-eye combat, all authorable: scene camera
`"mode": "third_person"`, `combat.camera` rig for encounters (default `overhead`;
`isometric` available). On encounter start the scaffold swaps rigs, frees the cursor,
and suppresses WASD (`GameplayCameraController` gained `driveCharacter` � frame without
steering; zero latched control on the suppression EDGE only, because the TurnActor drives
the body through the same `setControlInput`); on end it restores rig + look angles.
Measured geometry (cam-vs-player): exploration 4.4 up / 4.3 behind @ -30 deg ->
combat 30.0 up / 0.0 horiz @ -89 deg, WASD moved 0.0 -> restored 3.0 / 4.3 @ -30 deg,
quest completes. Three bugs found+fixed on the way: (1) `/api/state` camera read
InputManager's stale free-cam copy, not the rig-driven Graphics::Camera (retro-explains
finding 3); (2) latched control input walked the character through combat; (3) the
tactical phase scrambles InputManager pitch - look is snapshot/restored with the rig.
Evidence: hv_combat_probe.py (now with adaptive steering re-calibration - the following
camera changes key directions as it swings) + hv_combat_evidence.json + hv_combat.log.

## 6d. Increment 7 � mutual facing (2026-08-14)

Speakers and combatants LOOK at each other: interact snaps player and NPC to face each
other (drive suppressed during dialogue so camera-coupled facing cannot stomp it);
start_combat squares every combatant toward the nearest opposing-side combatant; a
surviving defender snaps to face its attacker on every hit. Convention = CharacterTurnBody
(model +Z, yaw = atan2(dx, dz)); `facing_yaw` added to get_player_state (getYaw - note
getCurrentYaw is private). Measured: combat square-off err 0.000 rad vs computed bearing;
dialogue facing 0.785 vs 0.785 expected, stable over 2 s at 10 Hz (micro-probe - the full
run first read the camera-coupled value because probe CALIBRATION walked the player out of
interact range before pressing E, a probe-flow bug, not a game bug; reorder noted). NPC-side
facing is code-reviewed only (not API-observable).

## 6e. Increment 8 � the HUD comes alive in the standalone (2026-08-14)

The fail-closed HUD panels now have live providers in generated games: combat.* (inCombat,
playerTurnActive, roundText, turnLabel, budgetText, hitChanceText, turn_order with per-row
HP/initiative), objectives.any + objectives ([x] markers), hotbar.any + hotbar (icons +
counts + selected slot) - the same registrations the editor makes (Application.cpp ~5793),
wired to the standalone's own subsystems. Runtime: all 8 HUD panels load (hud_health/
hotbar/objectives/dialogue/combat_banner/combat_order/combat_turn/combat_action), the
full playthrough completes with providers executing every frame including in-combat.
HONEST DEPTH: providers are functionally live (exercised, no crash, panels gated open by
live data); the rendered PIXELS are not machine-verified - no screenshot path on the
standalone API. Visual confirmation = watch the next run.

## 6f. Increment 9 - CLICK-TO-MOVE/ATTACK (2026-08-14): combat is played with the mouse

PlayerTurnController gained resolvePick/screenOf/requestPickAt - engine-side, projection-
agnostic (inverse view-projection, identical under perspective third-person and the ortho
tactical overhead): a living enemy combatant within 32px of the cursor resolves Attack
(nearest wins); otherwise the cursor ray intersects the ground plane for Move. TWO consumers
share the one path: the shipped game binds LMB on the player turn (edge-triggered, cursor
already free in combat), and the test API exposes combat/player_pick {x,y} +
combat/screen_of {entity_id} so probes fight by CLICKING. Measured on Hearthvale under the
overhead rig: click_move (803,495) -> move closed the gap, then five click_attack (661,355)
-> attack at the rat''s projected position until it died; encounter self-resolved; quest to
victory at fighter 2. A wrong NDC y-flip would have resolved none - the kill is behavioral
proof of the projection in both camera modes. The last big BG3 feel gap is closed.

## 6g. Increment 10 - PARTY MEMBERS (2026-08-14, partial - honestly scoped)

SHIPPED + measured: the join_party action (dialogue-driven recruiting - Bram joins from a
conversation choice), party persistence across scenes (members respawn at the player''s side
on world-scene load), start_combat AUTO-ENLISTS living party members player-side (measured:
3 combatants ["npc_Bram","player","npc_Rat"]), and the CombatAI ally-turn INFRASTRUCTURE
(waits only on the human''s entity via setPlayerEntityId; side-aware target acquisition -
legacy hosts unchanged).

THE HUNT (the increment''s real yield): "Bram won''t talk" -> engine root cause found via a
new tryInteract diagnostic sweep (dumps every npc-typed entity with dist/cast/radius when
nothing is in range): **InteractionManager::update''s playerFront DEFAULTS to +Z**, silently
arming a north-facing 90-degree view cone - ANY NPC south of the player was uninteractable.
Every earlier symptom (settle-time, first-visit, steering correlations) was coincidence of
geometry. Scaffold now passes an explicit zero front = BG3 proximity interact. Also fixed:
the tryInteract log''s {:.3f} spec (third literal-format bug found; args were shifting).

OPEN (next session): Bram''s OWN combat turn stalls in execution - the AI binds his turn but
he never attacks; leading suspect is his Idle NPC BEHAVIOR fighting the TurnActor for
setControlInput each frame (the rat, also idle, DOES act - asymmetry unexplained). Probe
lesson recorded in-file: combat/state player_turn is SIDE-based - drive the probe off
current_entity == player. Evidence: hv_combat.log (3-combatant encounter, joined-the-party
lines) + bram2.log-style diagnostic dumps.

## 6g-addendum - PARTY COMPLETE (2026-08-19): Bram fights

The stalled ally turn was NOT a behavior conflict: the companion respawn passed
animFile "" to NPCManager::spawnNPC, which (unlike the game.json loader) does NOT default
an empty path - the cellar Bram had no rig, so the TurnActor could never bind his body and
his turn stalled the encounter. One line (explicit humanoid.anim) closed it. Measured:
3-combatant encounter, "NPC npc_Bram hits npc_Rat for 2 (1d4) damage (roll 14 vs AC 14)" -
an autonomous ally turn with its own d20 - encounter self-resolves, quest to victory at
fighter 2. The full party loop is live in shipped games: dialogue recruit -> cross-scene
travel -> auto-enlist -> autonomous ally combat. ENGINE NIT for triage: spawnNPC should
default an empty animFile like the loader does (two spawn paths, two conventions).
(NIT CLOSED 2026-08-19 in a98bf7ee: spawnNPCWithBehavior now defaults empty animFile.)

## 6h. Increment 11 - PITCH-INVERSION ROOT CAUSE + COMPANION FOLLOW (2026-08-19)

**The under-floor camera is dead at the root.** It was an InputManager `mouseCaptured`
LEAK: GameplayCameraController set the flag every driving frame and never cleared it when
`driveCharacter` went false (combat/dialogue). The host frees the OS CURSOR for
click-targeting, but that is WindowManager state - the separate InputManager flag stayed
latched, so every mouse move made to click an enemy kept integrating into yaw/pitch until
pitch pinned at the +-89 clamp; restoring the exploration rig then framed the camera from
under the floor. Fix: capture is symmetric with driving (`setMouseCaptured(driveCharacter)`
for always-on-look schemes; firstMouse re-latches on the edge so re-capture never
integrates the cursor-park jump). Red-before-green: GameplayCameraControllerTest's RED run
reproduced pitch 89 + yaw scramble exactly; GREEN 3/3 after the fix. The scaffold's
snapshot/restore workaround is REMOVED - the root fix is the only mechanism, and the L4
proved the combat round trip unmasked with a positive-control probe (posted WM_MOUSEMOVE
sweeps scramble the exploration camera on demand, move the combat camera by exactly 0.0,
and post-combat look == pre-combat). Evidence: docs/evidence/hearthvale/pitch_follow_*.

**Companions FOLLOW in exploration (BG3-style).** NPCBehaviorType::Follow ->
PatrolBehavior::setFollowMode (reuses the whole patrol nav stack; 3.0u deadzone +
hysteresis, repath at 2.0u of target drift, 40u catch-up teleport; registry lookup, NOT
ctx.getEntityPosition - that helper returns ORIGIN for missing entities). game.json NPCs
can author `"behavior": "follow"`. Scaffold party respawn now uses Follow. Measured:
distance bounded while walking (1.93-3.71u), settles bit-still at 3.18u.

**Second bug, found by the L4's own vacuity guard:** NPC behaviors kept running during
turn-based combat - Follow's per-frame velocity writes fought the CharacterTurnBody (same
setMoveVelocity funnel) and STALLED the encounter; the first probe run's "post == pre"
camera pass was two mid-combat frames. Fix: NPCEntity::setBehaviorSuspended (+
NPCManager::forEachNPC); the scaffold suspends every NPC on the combat-enter edge and
resumes on exit. CAM2 now requires the encounter RESOLVED + third-person camera shape -
a stalled encounter can never fake that pass again.

## 6i. Increment 12 - SPELLCASTING in the shipped game (2026-08-19)

The rules stack was already complete (PlayerTurnController::castSpell: action budget,
cantrip scaling, save/attack-roll resolution, AoE, heals, release-frame application;
SpellRegistry/SpellAnimMapper/SpellVfxMapper/VfxDirector) - but NONE of it was reachable
in a shipped game: the registry only auto-loaded in the EDITOR (every standalone cast
would fail "Unknown spell"), the cast-visual executor lived only in Application.cpp, and
the combat API had no cast command.

Shipped: scaffold boot-loads resources/spells, ports playCastVisual (cast animation via
SpellAnimMapper plan -> VFX + damage at the RELEASE frame - a spell looks the same shipped
as in the editor), tracks casterLevel from the live sheet (save DC stays the controller
default 13 = 8 + prof 2 + mod 3, standard level-1 full caster; sheet-derived DC is a
follow-up); GameApiService gains POST combat/player_cast {spell_id, target_id}.

Hearthvale's player is now a CLERIC (5e XP thresholds are class-independent - the 300 XP
= level-2-on-turn-in beat is untouched) and the cellar fight can be won BY MAGIC:
red-first (unknown action on the old exe), then 5/5 - guiding_bolt one-shot the rat for
15 (4d6, real d20: the prior run logged a miss for 0), cast accepted from [out of reach]
with the action visibly spent on the HUD, VfxDirector emission + anim plan per cast, kill
XP on the cleric sheet. Evidence: docs/evidence/hearthvale/spellcast_* + hv_spellcast_*.
Checker lesson recorded there: Release-visible observables only (LOG_DEBUG lines are not
evidence in a Release run).

Open next in this area: spell HOTBAR/UI for human players (the API path is proven; a
mouse-driven cast picker is the missing expression), NPC/companion casting via CombatAI,
slot tracking through SpellcasterComponent (casts currently spend only the action).

## 6j. Increment 13 - SPELL HOTBAR: the mouse casts (2026-08-19)

game.json `progression.spells` (SpellRegistry ids, unknown ids dropped LOUDLY) builds a
combat spellbar: a vertical button stack on the right edge (spell names from the
registry), click ARMS a spell (ember highlight via the per-element bg override — the
theme feature doing live UI state), the next enemy click routes through castSpell instead
of the melee pick; ground click cancels; bar tears down on the combat exit edge. Clicks
route UI-FIRST in the combat handler (injectClick, the only real-click path into the
UISystem during combat gameplay).

Probe-verified through the REAL input path — posted WM_MOUSEMOVE parks the cursor on the
rat, an injected LMB lands in the combat click handler: "Combat click -> cast
'guiding_bolt' at 'npc_Rat' (ok)", encounter resolved in 2 hotbar casts. Evidence:
docs/evidence/hearthvale/spellbar_* (armed screenshot: legible stacked bar, guiding_bolt
glowing).

Three findings, each caught by a failing probe check (full detail in spellbar_result.txt):
load-order (spells parsed before the registry loaded — the loud-drop guard caught it);
glfwGetCursorPos polls the OS cursor live and diverges from the message-fed input stream
(combat clicks now read InputManager::getCurrentMousePosition); and a freeLayout UIPanel
consumes EVERY in-bounds click (modal semantics) — a fullscreen transparent overlay root
silently eats the world's clicks, so overlay roots must be sized to their content.
That last one is an ENGINE footgun worth a click-through flag eventually.
