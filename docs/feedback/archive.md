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
