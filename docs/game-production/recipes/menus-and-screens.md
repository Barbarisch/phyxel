# Recipe: menus & required screens

**Satisfies milestones:** `main_menu`, `pause_menu`, `options`, `victory_screen`, `game_over_screen`,
`credits` — the "finished game" UI layer. **Genre:** any. These are the screens a game needs to read as
*shipped* rather than a prototype.

Read `phyxel-mechanics` for the menu/HUD tool details; this is the ordered playbook. Two patterns exist
in Phyxel — **don't mix them**:
- **Shell screens** (built-in `ScreenState`: Intro → MainMenu → Playing → Pause → Victory → Credits) —
  ship by default (`resources/.../*_screen.json`); driven by triggers (`show_victory`/`show_credits`)
  and the pause key. Least work: customize the stock screens.
- **Menu scenes** (`sceneType:"menu"` with a `menuLayout`) — a full game-specific menu as its own scene,
  with buttons that `transition_scene` / `open_submenu`. More control; use for a bespoke main menu.

## 1. Design-first (GAMEPLAN.md → UI / Screens)
List each screen and its buttons/flow: main menu (Start / Continue / Options / Credits / Quit), pause
(Resume / Settings / Quit-to-menu), options (audio volumes / graphics / controls / accessibility),
victory + game-over (with Retry / Menu), credits. Note which use the shell vs a menu scene.

## 2. Main menu
- **Simplest:** keep the shell — the default `mainmenu_screen` already gives Start/Options/Quit.
  Customize its labels/branding in the project's screen JSON.
- **Bespoke:** add a `sceneType:"menu"` scene as the `startScene` with a `menuLayout` (buttons →
  `transition_scene` to the first level). Labels support `{{playtime}}` / `{{story.<var>}}` tokens.

## 3. Pause / options / accessibility
- Pause = the shell pause screen (pause key) or a pause menu layout. Wire Resume / Settings / Quit.
- Options = the settings screen: **audio volume sliders (separate music/SFX)**, graphics, and
  **remappable controls + subtitles + text size** — these double as the `accessibility` milestone, so
  build them *now* (ordering-critical).

## 4. Victory / game-over / credits (via triggers)
Wire the win/lose triggers to the shell screens:
`triggers[].then = [{"type":"show_victory"}]` and `[{"type":"show_credits"}]`; a lose trigger routes to
the game-over screen (or respawn). This also satisfies `win_condition` / `lose_condition`. Give victory
and game-over a Retry / Menu action.

## 5. Validate
- L2 (static): `production(op="validate")` confirms a menu scene or the shell menu exists, and that
  `show_victory`/`show_credits` triggers are wired.
- L3/L4 (runtime): launch, navigate to each screen, screenshot to confirm it renders and its buttons
  work. Record with evidence.
- Set each screen milestone via validate; mark `feel="passed"` after a polish pass (button hover/click
  sound, transitions eased, not linear pops).
