# Feedback archive

Triaged feedback entries, moved here from `inbox.md` once handled or folded into the roadmap
(`docs/AgentContext.md`). Kept for history; their date/project/type headers are preserved.

---
## 2026-06-06 — TestVideoGame1 — feature-request
No declarative win-condition / game-event trigger system in game.json. Building a complete game loop (intro menu -> main menu -> world -> win -> credits -> menu) works end-to-end via multi-scene menu scenes (transition_scene / quit_game button actions) EXCEPT the win condition: there is no data-driven way to say 'when <gameplay event> occurs, complete an objective and/or transition_scene to X'. Today the only way to advance from gameplay to a credits/victory scene on a gameplay event is hand-written C++ in the generated project's onUpdate. Requested: a data-driven trigger system (condition -> action) usable from game.json and the MCP game-building tools, e.g. trigger {when: player_jumped | objective_complete | entity_reached_region | timer, then: complete_objective | transition_scene | quit_game}. This would let the whole game shell be authored no-code.

> **RESOLVED (feature/game-triggers):** `TriggerSystem` — declarative `{when, then[], once}`
> in game.json (top-level or per-scene) + MCP `add_trigger`/`list_triggers`/`remove_trigger`.
> Events / timer / entity_reached_region conditions; complete_objective / fail_objective /
> transition_scene / quit_game actions (+ standalone show_victory / show_credits). Hosted by
> the editor AND the generated standalone game. Acceptance: TestVideoGame1's win condition is
> pure data — jump → trigger → credits.

## 2026-06-06 — TestVideoGame1 — feature-request
No gameplay events are surfaced for win-condition detection. A player jump is consumed internally (SPACE -> playerCharacter_->jump()) but never emitted: poll_events reports only entity/voxel/region/save events, and there is no MCP query for player jump/grounded/vertical-velocity state. This makes it impossible to detect simple gameplay actions (jump, land, reach height) over MCP to drive a win condition, and there is no event for a future declarative trigger system to subscribe to. Requested: emit gameplay events (at minimum player_jumped, player_landed) into the poll_events stream, and/or expose player kinematic state (grounded, velocity.y) via a get-player-state MCP tool.

> **RESOLVED (feature/game-triggers):** `player_jumped` / `player_landed` emitted into
> poll_events (edge-detected centrally in AnimatedVoxelCharacter::update); `get_player_state`
> MCP tool + `/api/character/player_state` (position, velocity, grounded, FSM state).
> `objective_complete` is also emitted now (ObjectiveTracker::onCompleted).

## 2026-06-06 — TestVideoGame1 — feature-request
No built-in front-end screens for Intro/Splash, Victory, or Credits. The standalone ScreenState enum (GameScreen.h) has MainMenu, Playing, Paused, Inventory, Settings, KeybindingRebind, Loading -- but no Intro or Victory/Credits/GameComplete state. These can be worked around with menu-type scenes, but a packaged single-scene game cannot show an intro before the menu or a credits screen on win without going multi-scene. Minor: a built-in 'GameComplete/Victory' screen plus a standard 'show credits then return to main menu' flow would round out the minimal game lifecycle.

> **RESOLVED (feature/game-triggers):** `ScreenState::Intro/Victory/Credits` +
> `renderIntroScreen` / `renderVictoryScreen` / `renderCreditsScreen` (GameMenus house style).
> Generated games start at Intro (title + description tagline) and get the standard
> victory → credits → main-menu flow; enter Victory via `screen_.showVictory()` or a
> `show_victory` trigger action.

## 2026-06-06 — TestVideoGame1 — feature-request
Editor cannot preview menu-type scenes or scene transitions, so the game shell (menus + transitions) can only be exercised by a full create_project + cmake standalone build. Observed in the editor (phyxel.exe --project): (1) load_game_definition with a multi-scene scenes array returns success but registers NO manifest (list_scenes -> has_manifest:false); the MCP load_game_definition schema also does not formally accept scenes/startScene/playerDefaults. (2) add_scene DOES register scenes (manifest appears), but transition_scene to a menu scene returns success while get_active_scene stays No active scene and the viewport keeps showing the editor -- GameMenuRenderer never displays. Net: there is no in-editor play/preview mode for the menu+scene front-end, blocking fast no-code iteration. Requested: an editor play-mode (or headless menu render) that activates the scene manifest and renders menu scenes + processes transition_scene/quit_game, so the full launch->intro->menu->world->credits flow can be verified without packaging.

> **RESOLVED (feature/game-triggers):** four scene-system bugs fixed — SceneManager::update()
> was never pumped (editor + standalone template now pump per frame); setSubsystems() had no
> caller (persistent GameSubsystems, refreshed per frame); SceneDefinition::fromJson ignored
> the documented nested "definition" key (payloads silently dropped); menu scenes were drawn
> into a background-sorted ImGui window that the editor dockspace occluded (GameMenuRenderer
> foreground mode). MCP load_game_definition schema now accepts scenes/startScene/
> playerDefaults/globalStory/transitionStyle/triggers. The full intro → menu → world →
> jump-to-win → credits flow now runs AND renders in the editor with no packaging.

## 2026-06-06 — TestVideoGame1 — bug
package_game.py produces an incomplete, non-runnable distributable for a multi-scene game. Running build\Debug\<game>.exe works because the CMake POST_BUILD copies the full shaders, resources, worlds, game.json and engine.json. But package_game.py with --project-dir and even --all-resources omits critical files so the packaged exe exits immediately: game.json is never copied into the output; resources fonts dir is never copied so menu text has no font; root resource JSONs needed at boot such as materials.json and mc_texture_map.json are trimmed out causing a crash; the per-scene worldDatabase game_world.db is not copied; and it warns about missing default anim names character_complete.anim character_box.anim character.anim that do not exist. The only way I got a runnable build was to mirror build\Debug into the output by hand. Please always include game.json, engine.json, resources fonts, the boot resource JSONs, and each scene worldDatabase.

> **RESOLVED (feature/gamedev-feedback-2):** package_game.py now always copies game.json
> (falling back to the project's own when not passed explicitly), the project's engine.json,
> resources/fonts/, the boot JSONs (resources/materials.json + resources/mc_texture_map.json),
> and each scene's worldDatabase (tolerating the legacy worlds/worlds/ layout). Stale default
> anim fixed: character_complete.anim -> resources/animated_characters/humanoid.anim (the old
> root character*.anim moved to resources/animated_characters/legacy/).

## 2026-06-06 — TestVideoGame1 — gotcha
Standalone template built-in shell supersedes menu-type scenes, with no guidance on which to use. A game authored with menu-type scenes (intro, main_menu, credits) plus a win trigger that does transition_scene to a credits menu scene works perfectly in the editor preview. But create_project.py generates a standalone whose built-in ScreenState shell (Intro, MainMenu, Victory, Credits) renders on top and drives the flow, so the custom menu scenes are never shown in the standalone. The two approaches collide: the built-in MainMenu New Game goes to ScreenState Playing without transitioning the SceneManager, so if startScene is a menu scene the player lands on the wrong scene. I had to set startScene to the world scene and change the win trigger from transition_scene credits to show_victory to make the standalone correct. Please document the recommended pattern (built-in shell + world startScene + show_victory/show_credits triggers, OR menu scenes only) and ideally make create_project detect menu scenes and not double up the shell.

> **RESOLVED (feature/gamedev-feedback-2):** the generated standalone now hosts a
> GameMenuRenderer + SceneCallbacks — sceneType:"menu" scenes render via the menu renderer
> (foreground) and the built-in ScreenState shell stays hidden underneath; when a world scene
> becomes ready the shell flips to Playing. menuSceneActive_ gates gameplay update/input and
> the cursor. Both patterns are documented in docs/GameCreationGuide.md ("Menus & Win/Lose
> Screens"), and create_project.py prints menu-scene guidance at scaffold time.

## 2026-06-06 — TestVideoGame1 — bug
MCP launch_engine binds the editor default API port 8090 instead of the project configured port 8093 from the .phyxel config, so MCP tools that target 8093 report engine not running or no project loaded, and a second launch collides on 8090 and fails to init. The phyxel up CLI launches on the correct project port. Please make launch_engine read and pass the project API port, or at least warn on mismatch. This cost real debugging time.

> **RESOLVED (feature/gamedev-feedback-2):** _launch_engine now passes --port derived from the
> MCP's own ENGINE_API_URL (PHYXEL_API_PORT), so the engine binds the port the MCP targets, and
> warns when the project's .phyxel/config.json apiPort differs. (Requires an MCP server restart
> to take effect.)

## 2026-06-06 — TestVideoGame1 — bug
Project auto-load does not register a multi-scene manifest. When phyxel up or open_project opens a project whose game.json has a scenes array, the Application auto-load runs the single-scene GameDefinitionLoader and logs chunks=0 structures=0 with has_manifest false, so list_scenes stays empty and menu scenes never appear. The manifest only loads when you POST api/game/load_definition or call MCP load_game_definition. Please have project auto-load detect multi-scene and route through SceneManager so opening the project shows the manifest.

> **RESOLVED (feature/gamedev-feedback-2):** Application::autoLoadGameDefinition now detects
> isMultiScene and routes through SceneManager (loadManifest + setWorldsDir + transitionTo
> startScene), mirroring the MCP load path. VERIFIED: launching --project TestVideoGame1 shows
> has_manifest:true with all 4 scenes and game_world active.

## 2026-06-06 — TestVideoGame1 — bug
Scene worldDatabase paths get double-nested on disk. A scene worldDatabase value of worlds/game_world.db is resolved relative to the project worlds directory, producing worlds/worlds/game_world.db. This also trips up packaging, which looks for worlds/game_world.db. Please pick one convention: treat worldDatabase as relative to project root, or document clearly that it is relative to the worlds dir, and make runtime and package_game agree.

> **RESOLVED (feature/gamedev-feedback-2):** SceneDefinition::getWorldDatabaseFilename strips a
> leading "worlds/"|"worlds\\" so "worlds/x.db" and "x.db" both resolve to <worldsDir>/x.db;
> package_game uses the bare filename and tolerates the legacy nested layout. Convention
> documented in docs/SceneSystem.md. VERIFIED: opened ...\worlds\game_world.db (single nest).

## 2026-06-06 — TestVideoGame1 — bug
Two debuggability gaps slowed me down. First, POST api/game/load_definition returns HTTP 500 with an empty body and writes no log line when the JSON payload is bad, for example when it contains a non-ASCII bullet character, so the cause is invisible. Please return 400 with the parse error and log it. Second, the standalone game writes no log file in its working directory and does not append to the shared engine log, so when a packaged exe exits immediately on startup there is nothing to diagnose. Please have the standalone write a log next to the exe.

> **RESOLVED (feature/gamedev-feedback-2):** the load_definition route returns 400 with the
> parse-error detail AND now logs a LOG_WARN with a body preview (EngineAPIServer.cpp). The
> generated standalone main() calls Logger::enableFileOutput(true, "<name>.log") so a packaged
> game that exits early leaves a log next to the exe.

## 2026-06-07 — TestVideoGame1 — feature-request
Game definitions cannot specify camera mode: game.json camera block (GameDefinitionLoader::loadCamera) only parses position/yaw/pitch, the set_camera HTTP/MCP API has no mode param, and standalone forces ThirdPerson after player spawn. A first-person game (e.g. a maze crawler) cannot start in first-person; the player must press V manually. Request: 'mode' field (FirstPerson/ThirdPerson/Free) in the camera block + set_camera API.

> **RESOLVED:** camera.mode ("first_person"/"third_person"/"free", PascalCase accepted) parsed
> by loadCamera (sets Camera::setMode, flags result.cameraModeSet); set_camera HTTP/MCP gained
> "mode" (validates, errors on unknown). The standalone only defaults to ThirdPerson when NO
> mode is authored anywhere in the definition. Unit-tested (LoadCameraMode); verified live —
> bad mode rejected, first_person puts the camera at the player's eyes inside the maze.

## 2026-06-07 — TestVideoGame1 — feature-request
Timer triggers (when.event=timer, when.seconds) fire correctly but are invisible to the player — there is no on-screen countdown HUD showing time remaining. Timed gameplay (e.g. 'escape the maze in 60s') is unfair without it. Request: a HUD countdown element, ideally data-driven (e.g. trigger option showHud:true or a hud block in the scene definition).

> **RESOLVED:** timer triggers accept "hud": true + optional "hudLabel";
> TriggerSystem::getActiveCountdowns() exposes remaining/total; UI::renderCountdownHud draws
> top-center (foreground list, red under 10s) in the editor AND the generated standalone
> (Playing state). list_triggers reports remaining seconds for hud timers. Unit-tested;
> verified live ("Escape the maze!  1:24.7" over the viewport).

## 2026-06-07 — TestVideoGame1 — feature-request
Menu scene text is static — no way to display dynamic values such as total elapsed/completion time on a credits screen. Request: variable interpolation in menuLayout labels (e.g. {{story.variableName}}) backed by story variables, plus a built-in elapsed-time/playtime variable, for speedrun-time-on-credits use cases.

> **RESOLVED:** GameMenuRenderer interpolates {{token}} in label/button text per frame via a
> host-provided onResolveVariable; both hosts wire {{playtime}} (unpaused gameplay clock,
> M:SS.s — editor m_playtimeSeconds / standalone elapsed_) and {{story.<var>}} (StoryEngine
> WorldState variables, all variant types stringified). Unknown tokens render literally.
> Verified live ("Your time: 1:53.4", "Score: 9001").

## 2026-06-07 — TestVideoGame1 — bug
move_entity on id 'player' reports success and moves an entity, but the animated character controller's position (what get_player_state and the entity_reached_region trigger resolver read) is NOT updated - the two silently desync. Workaround discovered: set_spawn_point + force_respawn teleports the real player. Request: either make move_entity reposition the character controller for the player entity, or add an explicit player teleport API.

> **RESOLVED:** move_entity("player") now targets the LIVE control character (the registry
> entry could be a stale duplicate from the pre-fix entity leak); AnimatedVoxelCharacter::
> setPosition is already a proper teleport (resets velocity/springs/step state). Verified
> live: move to (12,19,12) → get_player_state reports exactly (12,19,12), grounded.

## 2026-06-07 — TestVideoGame1 — bug
Animated entity count grows on every scene transition in a multi-scene game (observed 2 -> 6 -> 7 player-type entities across level transitions in the editor's World Outliner). transition_scene docs say entities are cleared on unload, but old player characters appear to accumulate. Possible leak; could affect performance and gameplay (stray visible characters).

> **RESOLVED:** SceneManager only INVOKES the SceneCallbacks unload hooks — nobody ever SET
> clearEntities/clearNPCs/endDialogue (an editor comment claimed "already handled"; it wasn't).
> Both the editor and the generated standalone now wire them (registry clear + entities clear +
> player null; NPC removal; dialogue end). Verified live: exactly 1 player across
> level1→level2→level3→game_over→level1 cycles (was 2→6→7). This also fixed the
> "Entity ID already taken: player" warnings.

## 2026-06-07 — TestVideoGame1 — bug
create_project.py crashes with UnicodeEncodeError on Windows cp1252 consoles: the menu-scene NOTE block prints a U+2192 arrow (line ~284). All project files are already written by then so the crash is cosmetic, but exit code 1 makes scripted use fail. Fix: ASCII '->' in console prints (the same non-ASCII discipline game.json requires).

> **RESOLVED:** both U+2192 arrows in console prints replaced with ASCII '->'; verified by
> scaffolding on a cp1252 PowerShell console (exit 0).

## 2026-06-07 — TestVideoGame1 — feature-request
Architecture lesson-learned (from the game-project side): the engine should expose overridable BASE CLASSES for the standard game functions - screen/menu shell, trigger action executor, HUD elements, game mode/flow, camera behavior - with sane default implementations, and real game projects should subclass and override them as the standard extension procedure. Today a game has only two extremes: pure data-driven game.json (hits walls like no countdown HUD or dynamic menu text) or editing the create_project.py GENERATED scaffold (~29KB MazeRunner.cpp embeds the whole shell: menu renderer wiring, ScreenState machine, trigger executor). Generated code copies mean engine fixes don't propagate (the 2026-06-06 JUMP scaffold and the 2026-06-07 scaffold already diverge) and any customization is a merge hazard. Proposal: move the generated shell logic into engine-side base classes (e.g. GameShell/GameCallbacks with virtual hooks per function), make the scaffold emit a thin subclass that just overrides what the game needs, and document when to stay data-driven vs when to override.

> **TRIAGED → ROADMAP (design item):** agreed — the scaffold-divergence pain is real and this
> round made it worse (more shell logic landed in the template). Folded into
> docs/AgentContext.md roadmap as "engine-side game-shell base classes": move the ScreenState
> machine, menu-renderer wiring, trigger executor and camera follow into an engine GameShell
> with virtual hooks; scaffold emits a thin subclass. Needs a design pass (hook inventory,
> data-driven vs override guidance) before implementation — not coded this round. Two of this
> round's walls (countdown HUD, dynamic menu text) were ALSO made data-driven so games need
> overrides less often.

## 2026-06-07 — TestVideoGame1 — bug
create_project.py scaffold initialization-order bug: the generated <Game>.cpp calls loadGameDefinition() (which runs SceneManager loadStartScene and fires cb.onMenuSceneLoaded) BEFORE gameMenuRenderer_ is constructed ~75 lines later. The callback null-checks gameMenuRenderer_, so for a game whose startScene is a menu scene the start menu silently fails to load and the built-in ScreenState Intro shell renders instead - exactly the double-shell situation the callbacks were added to prevent. Later menu scenes work (renderer exists by then). Fix: emit the GameMenuRenderer construction before the loadGameDefinition() call in the generated initialize(). Found packaging MazeRunner (startScene=intro menu scene) 2026-06-07; worked around by hand-reordering the generated code.

> **RESOLVED:** the template now constructs + wires gameMenuRenderer_ immediately after the
> RenderCoordinator, BEFORE loadGameDefinition(), with a comment explaining why the order is
> load-bearing. Verified: scaffolded a menu-startScene game and compiled it end-to-end.

## 2026-06-07 — TestVideoGame1 — bug
Standalone world-rendering parity bug (seen in BOTH packaged games: JUMP 2026-06-06 and MazeRunner 2026-06-07): the same game.json world renders correctly in the editor (project mode) - textured stone walls, lit terrain, sun shadows - but in the create_project.py standalone the terrain is washed-out/overbright white, structure voxels are near-black featureless, and the sky is pure black. World DATA is correct in the standalone (log: 'World: generated 1 chunks (Flat, seed=101)', 'structures=22', player spawned) so this is purely render-pipeline state. Evidence points at initialization the editor does that the generated standalone never does: DayNightCycle lives in the shared RenderCoordinator but the editor appears to configure sun/ambient (and the Vulkan clear color is hardcoded black {0,0,0,1} for both, so the editor's sky must come from lighting state too). Suggest auditing editor-only render init (day/night enable, sun direction/color, ambient strength, atlas/material application) and moving it into RenderCoordinator/EngineRuntime defaults so standalones match the editor - ideally as an overridable base-class hook per the base-classes feedback item. Repro: package MazeRunner, New Game -> Level 1, compare with editor screenshot of scene level1.

> **RESOLVED (root cause was the post-process chain, not lighting init):** the editor viewport
> displays the RAW offscreen scene texture; the swapchain post-process pass is ONLY visible in
> standalones, so its bugs shipped unseen: manual pow(1/2.2) onto an SRGB swapchain (double
> gamma → washed-out), un-thresholded bloom (adds a blurred copy of the whole frame ≈ 2×
> brightness), SSAO whose depth-derivative normals degenerate at the floor horizon (dark band
> across screen center), plus a Reinhard tonemap the editor look never had. post_process.frag
> is now an editor-parity composite (scene + OIT transparency only; the disabled effects are
> documented in-shader for deliberate re-enable). Verified on the deployed MazeRunner itself by
> hot-dropping the fixed .spv into its shaders/ dir (no rebuild needed) + scripted
> click-through to Level 1: band gone, stone properly textured; editor unchanged. The black sky
> is the same clear color in both (not a bug). The "no walls visible" part of the repro was the
> forced third-person camera embedded inside a maze wall (back-faces culled) — fixed separately
> by camera mode (first_person).

## 2026-06-07 — TestVideoGame1 — bug
Scaffold default-player collision: when startScene is a menu scene, the generated initialize() sees no player after loadGameDefinition() (menu scenes spawn none) and creates a fallback default player at (16,25,16). Every world scene that later loads spawns its own definition player, producing 'EntityRegistry: Entity ID already taken: player' warnings (observed in the packaged MazeRunner log) and a stray duplicate character in the world. The default-player fallback should be skipped for multi-scene games (or deferred until a world scene loads without defining a player). Related to the entity-accumulation-across-transitions bug filed 2026-06-07.

> **RESOLVED:** the template skips the default-player fallback whenever the SceneManager has a
> manifest (multi-scene): each world scene spawns its own definition player (or playerDefaults).
> Combined with the unload clearEntities fix, "Entity ID already taken: player" is gone —
> verified: exactly one cleanly-registered player per world scene.

## 2026-06-10 — TestVideoGame1 — feature-request
game.json structure fills only place voxels into EMPTY air and silently fail against existing terrain or earlier fills (observed: fill_region over a Flat-world floor at the terrain surface returned placed:0 failed:432; in-manifest fills behaved the same, e.g. glass windows placed at coords already filled by a wall, or a glow firebox placed inside an already-filled stone hearth). Request: a 'replace': true flag on fill structures (and fill_region), plus surfacing placed/failed counts in the GameDefinitionLoader 'structures=N' log line so silent collisions are visible.

> **RESOLVED:** fill structures and fill_region (sync + async job paths + MCP schema) accept
> "replace": true — occupied voxels are removeCubeFast'd (deferred re-mesh) then overwritten.
> The loader now logs per-fill placed/failed at INFO with the coords and a "(occupied voxels
> skipped — add \"replace\": true to overwrite)" hint when a non-replace fill collides.

## 2026-06-10 — TestVideoGame1 — gotcha
The phyxel-world skill's material list is outdated: it lists Cork and Rubber which do not exist in the engine (an unknown material like Cork renders as a magenta missing-texture checkerboard instead of erroring at load/validate time). The actual palette (list_materials, 19 entries) also includes Log, Cobblestone, StoneBricks, Bricks, Sandstone, Gravel, Sand, Gold and Mirror, none of which the skill mentions. Fix the skill doc, and consider making validate_game_definition / fill loading reject unknown material names.

> **RESOLVED:** phyxel-world skill AND the engine repo's own CLAUDE.md table (also stale!)
> rewritten from resources/materials.json (19 materials, real physics values).
> GameDefinitionLoader::validate now rejects unknown structure materials ("Unknown material 'X'
> ... see list_materials") and the fill loader skips them with LOG_ERROR instead of rendering
> magenta. Registry-empty guard keeps bare unit tests unaffected.

## 2026-06-10 — TestVideoGame1 — feature-request
Dialogue trees support only id/speaker/text/emotion/choices/nextNodeId - no actions, conditions, or variables - and declarative triggers support only 'timer' and 'entity_reached_region'. So a core RPG pattern like 'convince 3 NPCs, then the win condition unlocks' cannot be expressed: conversation outcomes leave no machine-readable state. Built TestVideoGame1 (The Gilded Tankard) with knowledge-gating as the workaround (NPC dialogue TEXT tells the player where a hidden entity_reached_region win spot is, but nothing stops a player who skips the dialogue). Request, in preference order: (a) node-level actions on dialogue nodes (set_story_variable, complete_objective, transition_scene), (b) a 'dialogue_node_reached' trigger event keyed by tree/node id, (c) choice-level conditions on story variables so trees can branch on earned state.

> **RESOLVED — all three, live-verified end-to-end:** (a) DialogueNode "actions" execute on
> node entry through the SAME executor as trigger "then" entries (set_story_variable /
> complete_objective / fail_objective / transition_scene / quit_game;
> TriggerSystem::executeHostAction shares the vocabulary; set_story_variable action added to
> the editor + scaffold executors). (b) every node shown fires dialogue_node_reached
> {tree,node,speaker}; TriggerSystem::onEvent now matches ANY non-reserved "when" key against
> the event payload (was id-only), so {"when":{"event":"dialogue_node_reached",
> "node":"give_secret"}} works. (c) DialogueChoice "condition" on story variables
> (equals/not_equals/gte/lte/exists), evaluated via a host-wired StoryEngine resolver; missing
> variables FAIL CLOSED (gated choice hidden until earned). Hosts (editor + scaffold template)
> wire setEventSink/setActionExecutor/setVariableResolver. Verified live: gated choice hidden ->
> earn_trust node action sets variable -> choice appears -> give_secret completes the objective +
> payload-matched trigger fires. Documented in GameCreationGuide + phyxel-characters skill.
> BONUS: fixed add_trigger/remove_trigger MCP handlers (NameError: 'arguments' vs 'args' —
> add_trigger via MCP never worked).

## 2026-06-10 — TestVideoGame1 — gotcha
Process gap, from building a tavern game: the session hand-built ALL furniture (bar counter, tables, stools, barrels, hearth) from full-size voxel fill structures, then discovered afterwards that the template catalog already had purpose-built assets for nearly every one of them (tavern_bar, tavern_table, table_wood, chair_wood, stool, bench_wood, barrel, crate_wood, fireplace, candle_holder, lantern, torch_wall, tavern_test building - 50 templates with subcube/microcube detail far beyond what fills can do). Nothing in the world-building workflow points at the catalog: the phyxel-world skill presents fills+templates as equals without a furnish-with-templates-first rule, and the playtest skill never mentions assets. Suggest: (a) phyxel-world skill should say explicitly 'interiors/props: search_templates FIRST, fills are for shells/terrain only'; (b) the game-definition docs should show a structures example mixing fills (walls) with type:template entries (furniture); (c) consider having validate_game_definition or the loader warn when a definition contains many small fills that look like hand-built furniture. Also minor friction: list_templates errors with 'No game project is loaded' while a menu scene is active (list_generated_templates works) - catalog browsing should not require an active world.

> **RESOLVED (a, b, + the friction; c dropped):** phyxel-world skill now leads with
> "Templates FIRST for interiors & props — fills are for shells/terrain ONLY" and advertises
> the tavern set; GameCreationGuide structures section shows a mixed fills+templates interior
> example. list_templates added to the MCP _NO_PROJECT_TOOLS whitelist (catalog browsing works
> during menu scenes / before a world loads). (c) — a loader heuristic warning on
> "furniture-looking fills" — dropped as over-clever/low-value.

## 2026-06-10 — TestVideoGame1 — feature-request
Asset wishlist from building a tavern dialogue game (The Gilded Tankard) - items the session wanted but the template catalog lacks; queue for generation/authoring as a 'tavern interior' set: (1) tankard/beer mug (bar top + table clutter, the game is literally named after one), (2) ale/wine bottle, single, (3) bottle shelf row / back-bar rack with bottles (the session faked this with a Wood/Glass/Wood fill sandwich), (4) large keg with tap spigot (bigger than barrel, sits behind the bar), (5) hanging tavern sign (exterior, post + board), (6) chandelier / hanging candle wheel (interior overhead light), (7) rug/floor mat (since fills cannot recolor terrain floors, a flat template is the only way to vary interior flooring), (8) cellar trapdoor/hatch (would have been a better hidden-secret marker than a glow voxel), (9) food set: plate, bread loaf, cheese wheel (table dressing), (10) sack/grain bag (storeroom clutter), (11) wall decor: mounted antlers or framed painting, (12) serving tray. Existing set that should be advertised to sessions: tavern_bar, tavern_table, chair_wood, stool, bench_wood, barrel, crate_wood, fireplace, candle_holder, lantern, torch_wall.

> **ROADMAPPED:** queued in docs/AgentContext.md as the "tavern asset batch" for a BlockSmith
> /generate session (12 items). The "advertise the existing tavern set" half is done — the
> phyxel-world skill + GameCreationGuide now name those templates explicitly.

<!-- ===== Game-dev feedback round 5 — UIShowcase, 2026-06-18 — triaged & roadmapped in docs/AgentContext.md ===== -->

## 2026-06-18 — UIShowcase — bug
Editor never sets up the game HUD for MULTI-SCENE games. `Application::autoLoadGameDefinition()`
returns early in its multi-scene branch (line ~5331), *before* the single-scene path's
`setupGameHud(gameDef)` call (line ~5359); the multi-scene branch of the `load_game_definition`
command handler (line ~13118) likewise never calls it. Net effect: opening a multi-scene project
(menu `startScene` → world scene, like UIShowcase) via `--project`/`open_project` or
`load_game_definition` never runs `setupGameHud()`, so the default HUD, the HUD data providers
(`player.health`, `hotbar`, `objectives`, `dialogue`, `combat`), AND the game.json `combat.mode`
are all skipped. The standalone `GameShell` host (UIShowcase.cpp `onInitialize`) sets these up
unconditionally, so the editor renders strictly LESS than the shipping game. Because EVERY one of
these surfaces is a panel in `resources/ui/default_hud.json` — `hud_health` (HP bar), `hud_hotbar`
(per-material icons), `hud_objectives`, `hud_dialogue` (speaker/wrapped-text/choices), and the
combat set (`hud_combat_banner`/`hud_combat_order`/`hud_combat_turn`/`hud_combat_action` with the
End Turn button) — NONE of them render in-editor for a multi-scene game. Note this also kills the
**dialogue box**: standard dialogue TREES render through the data-bound `hud_dialogue` panel (the
ImGui `renderDialogueBox` path is gated to AI conversations only, Application.cpp ~2801), so a tree
conversation is fully functional at the state level (`/api/dialogue/state` shows active + choices +
typewriter) yet draws nothing on screen. Net: opening a multi-scene project leaves the entire
`/api/ui/...` HUD, objectives, combat-HUD, and dialogue-box surface dark. Fix: call
`setupGameHud(gameDef)` (and apply `combat.mode`) in BOTH multi-scene code paths
(`autoLoadGameDefinition` multi-scene branch AND the `load_game_definition` multi-scene handler),
not just the single-scene one.

## 2026-06-18 — UIShowcase — gotcha
Driving a menu scene's buttons through the editor's live menu preview is unreliable. Injected
clicks via `POST /api/ui/click` at the correctly-scaled coordinates (menu canvas is 1280×720,
scaled by `sx = ui.width()/1280`; here ×1.25) returned `{"consumed":true}` and `open_submenu`/
`close_submenu` panel switches worked, but a `transition_scene` button (New Game → game_world)
never fired the transition (no SceneManager transition logged), and submenu Back navigation went
unresponsive after a couple of interactions. Had to drive `POST /api/scene/transition
{"scene_id":"game_world"}` directly to enter the world. Worth confirming whether the editor's
Menu Editor live-preview wiring (`onMenuSceneLoaded` → `loadMenuInto`) handles `transition_scene`
actions the same as the standalone, or whether queued `ui_click`s race the scene pump.

## 2026-06-18 — UIShowcase — bug
The generated standalone scaffold ignores a game's top-level `hud` customization block.
`UIShowcase.cpp` (the scaffolded `GameShell` subclass) calls
`UI::loadHudInto(*getUISystem(), nullptr)` with a hardcoded `nullptr`, so it ALWAYS loads
`resources/ui/default_hud.json` and never the game.json `"hud"` array. The editor's
`setupGameHud()` correctly passes `gameDef.contains("hud") ? &gameDef["hud"] : nullptr`, so the
override works there (single-scene only, per the other bug). Net: the data-driven HUD-customization
feature (reposition/restyle health bar, add custom title labels via a top-level `"hud"` array) is
DEAD in shipped scaffolded games — they can't override the default HUD at all. Fix: the scaffold
template should parse game.json once and pass its `"hud"` block to `loadHudInto` (mirror
`setupGameHud`), and probably also honor `combat.mode` — i.e. migrate this wiring into `GameShell`
so every standalone inherits it rather than re-deriving the editor's logic.

## 2026-06-18 — UIShowcase — bug
The scaffolded standalone ships WITHOUT the engine default resources it needs, so a freshly built
`UIShowcase.exe` either crashes on boot or renders an empty world. Two gaps: (1) the project's
`shaders/` dir is EMPTY — no `*.spv` — so the app dies instantly right after "Framebuffers created
successfully" when the render pipelines try to load shaders (no error logged; just exit). (2) the
project's `resources/` had only the game's own `ui_banner.png` — missing `animated_characters/
humanoid.anim` (→ "Failed to load animated character", player + NPCs invisible), `ui/
default_hud.json` (→ "Default HUD not found … no HUD loaded", no HP bar/dialogue panel), `fonts/`
(→ bitmap-font fallback), and `textures/` + `materials.json`/`biomes.json` (→ "fallback checkerboard
texture"). Had to hand-copy all of these from `PHYXEL_ROOT` (`../../GitHub/phyxel/{shaders,resources}`)
into the project + build output to get a working game. Fix: either the project generator should
seed `shaders/` and the required `resources/*` defaults, or `CMakeLists.txt` should copy them from
`${PHYXEL_ROOT}/shaders` and `${PHYXEL_ROOT}/resources` in the POST_BUILD step (it currently only
copies the project's own — empty — `shaders/`/`resources/`).

## 2026-06-18 — UIShowcase — bug
The scaffolded `UIShowcase.cpp` never registers its player/entities with the renderer: it calls
`renderCoordinator_->setNPCManager(npcManager_.get())` but NOT
`renderCoordinator_->setEntities(&entities_)`. So `RenderCoordinator::entities` stays null,
`renderEntities()`/`renderInstancedCharacters()` skip the player (which lives in `entities_`, not the
NPCManager), and the world renders as empty terrain — the player character is completely invisible
even though it loads fine (skeleton segments build, third-person camera follows an unseen body).
Adding `setEntities(&entities_)` in `onInitialize` (right after `setNPCManager`) fixes it — player +
NPCs then render. Separately, the menu-scene path has a parallel omission: `cb.onMenuSceneLoaded`
builds a fresh `MenuActions` with `onTransitionScene`/`onQuit` but no `onResolveVariable`, so
`loadMenuInto` leaves every `{{token}}` literal on screen (`Gold: {{story.gold}}`, `{{playtime}}`).
Wiring `acts.onResolveVariable = gameMenuRenderer_->onResolveVariable` fixes that. Both are scaffold-
template bugs — every generated standalone inherits them. Consider moving entity-render wiring + the
menu resolver into `GameShell` so games don't re-derive it.

## 2026-06-18 — UIShowcase — bug
`close_submenu` (submenu Back) soft-locks the menu in the STANDALONE `loadMenuInto`/`UISystem` path
(distinct from the editor's `GameMenuRenderer`, which works). Root cause: `UISystem::handleInput`
delivers a SINGLE click to EVERY visible screen in one pass. `close_submenu`'s onClick calls
`menuShowOnly(ui, "menu:"+startPanel)` → shows `menu:main` mid-loop. Because screens live in a
`std::map` ordered `credits` < `main`, the same click is then handed to `menu:main`, and the Back
button (c_back/o_back at canvas y≈444, center of a 200×48 box) sits exactly over the main panel's
"Credits" button (y 410–458) — so it instantly re-fires `open_submenu credits`. Net: click bounces
credits→main→credits and the user is trapped (verified by driving real clicks + screenshots; Back
visibly does nothing). CORRECTION/precision (verified): it is ORDER-dependent, not just overlap-
dependent. The bug only triggers when the revealed `menu:<startPanel>` is iterated LATER in the same
map-ordered pass than the current submenu. Keys sort `credits` < `main` < `options`, so
**Credits→Back BOUNCES** (`main` > `credits`, re-visited same pass) but **Options→Back WORKS**
(`options` > `main`, so `menu:main` is already behind the iterator — confirmed on screen: Options
Back cleanly returns to main). So whether a given submenu's Back works is luck of the std::map order.
Fix: stop after a click is `consumed` (break the screen loop), or snapshot visibility before the loop
so a screen revealed by an onClick can't receive the same click.

## 2026-06-18 — UIShowcase — gotcha
The scaffold's pause/intro/victory/credits/settings screens render via ImGui
(`renderPauseMenu`/`renderIntroScreen`/…), while menu SCENES (`sceneType:"menu"`) render via the
data-driven UISystem. So pressing ESC in the world pops up a plain ImGui pause menu that looks
nothing like the game's styled data-driven main menu — feels like a different/"old" UI. Consider
giving GameShell a data-driven pause/credits path (or a game.json-authored pause menu scene) so the
in-world menus match the main menu's look.

## 2026-06-18 — UIShowcase — bug
Dialogue renders TWICE at once: the old ImGui dialogue box AND the new data-driven one. The scaffold
`UIShowcase.cpp::onRender` calls `imgui->renderDialogueBox(dialogueSystem_)` unconditionally while
dialogue is active, AND `resources/ui/default_hud.json` ships a `hud_dialogue` panel
(`visibleWhen:"dialogue.active"`, binds `dialogue.speaker`/`dialogue.text`/`dialogue.choices`) that
the UISystem also draws. So talking to an NPC stacks two overlapping speaker/text/choices boxes. The
ImGui path is meant only for AI conversations (cf. the editor gating `renderDialogueBox` to AI mode,
Application.cpp ~2801) but the standalone scaffold never gates it. Fix: in the scaffold, only render
the ImGui dialogue box when the data-driven `hud_dialogue` panel is absent (or gate it to AI
conversations like the editor). This was plainly visible on screen — see the testing-process entry
below; it's exactly the class of defect a real play-test catches and an API/state check does not.

## 2026-06-18 — UIShowcase — feature-request (testing process — HIGH PRIORITY / META)
Game-dev sessions keep shipping "verified" games riddled with obvious, observable defects because
there is no standard, thorough, automatic FUNCTIONAL play-test procedure — sessions lean on API/state
probes (`/api/dialogue/state` says "active+choices", so dialogue is "verified") and never actually
look at the running game and exercise it like a player. Concrete defects that slipped through in THIS
project and were only caught when the user played it (or on a careful screenshot pass): player
character never rendered (missing `setEntities`); empty `shaders/`+`resources/` → instant crash /
empty world; `{{story.gold}}`/`{{playtime}}` printed literally; menu **Back** buttons soft-lock;
**Continue** dead-ended into Credits; the world was a tiny void patch; and TWO dialogue boxes draw on
top of each other. Most are one screenshot away from obvious.

What's needed: a normal play-test procedure that Claude sessions ADOPT AUTOMATICALLY for every game,
and harness/tooling to make it cheap. It must (a) drive the actual built game, not just the editor
API — the standalone has no HTTP API, so input must be injected (this session bootstrapped it with
PowerShell: find the GLFW window by title via EnumWindows since MainWindowHandle is 0, screenshot via
CopyFromScreen, click via ClientToScreen+mouse_event at the 1280×720 canvas, keys via keybd_event;
worth shipping as a real tool); (b) exercise EVERY interactive element and state transition, not a
happy path; and (c) judge the rendered OUTPUT (vision pass over each screenshot), not just logs/state.

Standard checklist the procedure should encode:
  • Every button in every menu/submenu fires its action; every Back/close returns (no dead-ends or
    soft-locks); every state transition works (menu→world→pause→dialogue→back→quit).
  • Game functions "make sense": Continue continues, New Game starts fresh, Quit quits, Options/Credits
    do what they say.
  • Visual correctness: no duplicate/overlapping UI (the two dialogue boxes!), nothing missing
    (player/NPC actually visible, HUD present), all `{{tokens}}` resolved, no literal placeholders.
  • Behavioral/"feel" correctness: characters FACE EACH OTHER during dialogue; the camera frames the
    speaker; NPCs react to the player; no one stands in the floor / falls through.
Ship this as an engine-side "functional smoke test" agent/harness for generated games + bake the
checklist into the game-dev session instructions so thorough play-testing is the default, not an
afterthought. The bar: a session should not call a feature "verified" without having watched it work
on screen and tried to break it.

## 2026-06-18 — UIShowcase — bug (architecture — NO ImGui in gameplay)
ImGui must NOT drive ANY gameplay UI in a shipped standalone — it should exist only as a debug
fallback. Today the scaffold renders almost the ENTIRE non-menu-scene UI through ImGui
(`GameScreen`/`GameMenus` + `ImGuiRenderer`), and it looks like a different, unstyled app bolted onto
the data-driven menus. Full list of ImGui gameplay surfaces in `UIShowcase.cpp::onRender` that need to
move to the data-driven UISystem: `renderIntroScreen` (splash), `renderVictoryScreen`,
`renderCreditsScreen` (note: a SECOND credits UI separate from the data-driven `credits` submenu),
`renderMainMenu` (fallback main menu), `renderCountdownHud` (timer HUD), `renderPauseMenu` (the
"PAUSED" menu ESC pops up), `renderSettingsScreen`, `imgui->renderDialogueBox` (the duplicate
dialogue box — see its own entry), `imgui->renderSpeechBubbles`, and `imgui->renderInteractionPrompt`
(the "[E] interact" world prompt). Desired end state: every one of these is authored/rendered through
the UISystem (data-driven panels, like menu scenes and `default_hud.json` already are), the scaffold
carries NO ImGui gameplay calls, and ImGui is compiled/usable only as an opt-in debug overlay. This
is the umbrella behind several other entries (double dialogue, ESC pause look, pause overlay). Needs:
data-driven equivalents for pause/settings/victory/intro/credits + speech-bubble + interaction-prompt
+ countdown, and a GameShell that wires them so generated games never touch ImGui for gameplay.

## 2026-06-18 — UIShowcase — bug
HUD and world prompts stay on screen while the game is PAUSED. Opening the pause menu (ESC in the
world) leaves the `default_hud.json` HP bar AND the "[E] interact" prompt rendered underneath/over the
pause menu (observed on screen). Pausing should suppress gameplay HUD + interaction prompts (or the
pause menu should be a full data-driven screen that hides them). Tie-in with the ImGui-removal entry:
the pause menu itself is ImGui while the HUD is data-driven, so they don't coordinate visibility.

## 2026-06-18 — UIShowcase — bug (to verify/fix)
Dialogue "feel" defects worth a behavioral check the test pass should cover: when you talk to Elder
Maewyn the player and NPC do not clearly turn to FACE each other, and the camera does not reframe to
the conversation — you talk to an NPC while looking at the player's back. A polished dialogue start
should rotate both participants to face one another (and ideally ease the camera to a
conversation/over-the-shoulder framing). Flagging as the kind of "does it make sense to a player"
behavior automated functional testing should assert, not just "dialogue.active == true".

## 2026-06-18 — UIShowcase — gotcha
`/api/rpg/combat/end_turn` only RECORDS a pending player intent that the game loop applies via
`PlayerTurnController`; when an encounter is begun through the dev API
(`/api/rpg/combat/start` → `CombatDirector::beginEncounter`) rather than the normal gameplay flow,
that controller isn't engaged, so `end_turn` is a no-op (round/turn never advance). `/api/rpg/
combat/next_turn` (`CombatDirector::advanceTurn`) DOES advance directly and is the reliable way to
script turn/round progression in a test. The HUD's End Turn button presumably routes through the
engaged PlayerTurnController; an API-started encounter can't exercise that path. Consider making
`combat/end_turn` advance the director directly when no PlayerTurnController turn is active, or
document that API-driven encounters must use `next_turn`.

> **ROADMAPPED (round 5, 2026-06-18):** all 13 entries triaged into the "Game-dev feedback
> round 5" block in `docs/AgentContext.md`. Throughline: the generated standalone ships strictly
> LESS than the editor and ImGui is bolted over the data-driven UI — most items are round-5 scope
> for the "engine-side game-shell base classes" roadmap item (migrate into `GameShell` so games
> inherit it). Must-fix-first bugs flagged: empty `shaders/`+`resources/` (crash/void world),
> missing `setEntities` (invisible player), double dialogue box. New roadmap items: a functional
> smoke-test harness for generated games (input-injection tool + vision-judged checklist baked into
> session instructions — HIGH PRIORITY), and the editor multi-scene `setupGameHud` gap. NOT yet
> implemented — this is the to-do list for the next engine-dev push.

## 2026-07-12 — Emberwake — feature-request
Snowy grass block material + texture for snowy/tundra biomes. Survival games set in snow (e.g. the Emberwake dogfood) need snow-topped ground, but today there is no SnowGrass material and no snow biome in biomes.json (Perlin worlds render bare stone/grass). Add a snow-grass material (snow top + snow-dusted sides, matching the coursed-vs-varied rules) and a snowy/tundra biome so snow worlds generate correctly.

> **RESOLVED (2026-07-13, terrain-gen Increment 4, uncommitted→committed this change):** Added two
> grounded materials — **SnowGrass** (snow-dusted taiga ground: white top, snow-crust-over-dirt sides;
> the Snow biome's surface; keeps conifers) and **Snow** (bare permanent snowpack: matte white all
> faces; the alpine cap above the treeline; blocks flora). The premise was partly outdated — a **Snow
> biome already existed** in biomes.json, but it (and the lapse-rate alpine override) surfaced the
> glassy **Ice** material. Replaced Ice with SnowGrass/Snow and added a second, colder threshold
> `kTreelineTemp01` (−8 °C) below the 0 °C snow line so the override bands forest → SnowGrass (taiga) →
> Snow (bare cap). Textures via `tools/gen_snow_textures.py`; physics grounded (grounding-auditor:
> settled-snowpack density, packed-snow friction, near-neutral albedo, matte roughness). Verified:
> 5 terrain tests red→green (bareSnow 14695 / 0 treeline violations; flora onSnow 0 / onTaiga 83),
> full suite 2771 pass / 0 fail (MaterialRegistry counts synced 99→101), L4 runtime (Mountains
> generator placed SnowGrass on a cold peak, matte white, zero Ice), solution-auditor PASS.
> NOTE: existing worlds keep Ice unless regenerated (biomes bake into world.db on first load). Docs:
> `docs/TerrainGenerationV2.md` Increment 4 + CLAUDE.md Materials table. NOT done: a distinct Tundra
> snow treatment / snow-specific Gravel — Tundra still relies on the lapse override.

## 2026-07-12 — Emberwake — feature-request
Stable UUID / persistent-ID system for items, structures, placed objects, and entities. Every item/structure/object/entity should get a persistent unique identifier so a user OR the LLM can query, modify, or remove a SPECIFIC one by ID (e.g. get_object(uuid) / move / rotate / remove_object(uuid)), instead of addressing things by position/type/index which is ambiguous and brittle. Underpins reliable agent-driven world editing and lets the game-production tracker reference specific world content precisely.

> **RESOLVED (2026-07-13, shipped to main, commits 0013285…521a640):** Stable RFC-4122 v4 UUIDs across
> all four categories, in six audited slices. **Phase 0** `Core::Uuid` (generate + strict isValid, the
> strictness makes resolve-by-either-id unambiguous). **Phase 1** placed objects/structures: `PlacedObject.uuid`
> + a `resolveIdLocked` choke point so every placed-object tool resolves by legacy id OR uuid; rides the
> placed_objects blob; lazy backfill. **Phase 2a** entities: `EntityEntry.uuid` + resolve; spawn_entity
> returns it. **Phase 2b** runtime-entity persistence: new `RuntimeEntityStore` (runtime_entities table) +
> respawn-with-same-uuid on load (found+fixed a real m_nextAutoId reload-collision bug). **Phase 3** item
> instances: `ItemStack.instanceUuid` (mint rule + canMerge gate so uuid-stacks never merge), carried
> through drop→prop→pickup, equip→unequip, give/spawn mint+return (found+fixed a real creative-drop uuid
> duplication + an unwired equip route by running the L4). Full suite 2798 pass / 0 fail; L4 evidence in
> docs/evidence/uuid-phase2b-L4.md (entity persistence, item drop/pickup survival+creative, equip/unequip).
> Each slice solution-auditor-gated (core/1/2a/2b PASS; 3-wiring re-audit PARTIAL-PASS with the equip-handler
> gap then closed by the prescribed L4). Minor follow-up left: an explicit by-instance-uuid GET on get_item
> (get_inventory already surfaces per-slot uuids). Memory: [[uuid-identifier-workstream]].
