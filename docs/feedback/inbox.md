# Feedback inbox

Lessons-learned and feature-requests captured during Phyxel **game-dev** sessions, appended by
`phyxel feedback` (usually via the `/feedback` command). **Engine-dev** sessions consume this
via `/triage-feedback`: summarize, fold actionable items into `docs/AgentContext.md`'s roadmap,
then move handled entries to `archive.md`.

Entry format:

    ## <date> — <project> — <bug|gotcha|feature-request>
    <description>

---
## 2026-06-06 — TestVideoGame1 — feature-request
No declarative win-condition / game-event trigger system in game.json. Building a complete game loop (intro menu -> main menu -> world -> win -> credits -> menu) works end-to-end via multi-scene menu scenes (transition_scene / quit_game button actions) EXCEPT the win condition: there is no data-driven way to say 'when <gameplay event> occurs, complete an objective and/or transition_scene to X'. Today the only way to advance from gameplay to a credits/victory scene on a gameplay event is hand-written C++ in the generated project's onUpdate. Requested: a data-driven trigger system (condition -> action) usable from game.json and the MCP game-building tools, e.g. trigger {when: player_jumped | objective_complete | entity_reached_region | timer, then: complete_objective | transition_scene | quit_game}. This would let the whole game shell be authored no-code.

## 2026-06-06 — TestVideoGame1 — feature-request
No gameplay events are surfaced for win-condition detection. A player jump is consumed internally (SPACE -> playerCharacter_->jump()) but never emitted: poll_events reports only entity/voxel/region/save events, and there is no MCP query for player jump/grounded/vertical-velocity state. This makes it impossible to detect simple gameplay actions (jump, land, reach height) over MCP to drive a win condition, and there is no event for a future declarative trigger system to subscribe to. Requested: emit gameplay events (at minimum player_jumped, player_landed) into the poll_events stream, and/or expose player kinematic state (grounded, velocity.y) via a get-player-state MCP tool.

## 2026-06-06 — TestVideoGame1 — feature-request
No built-in front-end screens for Intro/Splash, Victory, or Credits. The standalone ScreenState enum (GameScreen.h) has MainMenu, Playing, Paused, Inventory, Settings, KeybindingRebind, Loading -- but no Intro or Victory/Credits/GameComplete state. These can be worked around with menu-type scenes, but a packaged single-scene game cannot show an intro before the menu or a credits screen on win without going multi-scene. Minor: a built-in 'GameComplete/Victory' screen plus a standard 'show credits then return to main menu' flow would round out the minimal game lifecycle.

## 2026-06-06 — TestVideoGame1 — feature-request
Editor cannot preview menu-type scenes or scene transitions, so the game shell (menus + transitions) can only be exercised by a full create_project + cmake standalone build. Observed in the editor (phyxel.exe --project): (1) load_game_definition with a multi-scene scenes array returns success but registers NO manifest (list_scenes -> has_manifest:false); the MCP load_game_definition schema also does not formally accept scenes/startScene/playerDefaults. (2) add_scene DOES register scenes (manifest appears), but transition_scene to a menu scene returns success while get_active_scene stays No active scene and the viewport keeps showing the editor -- GameMenuRenderer never displays. Net: there is no in-editor play/preview mode for the menu+scene front-end, blocking fast no-code iteration. Requested: an editor play-mode (or headless menu render) that activates the scene manifest and renders menu scenes + processes transition_scene/quit_game, so the full launch->intro->menu->world->credits flow can be verified without packaging.

