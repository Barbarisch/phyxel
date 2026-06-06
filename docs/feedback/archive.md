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
