# Game-HUD System — Remaining Work / TODO

Status as of the `feature/hud-system` merge (2026-06-17). The HUD system design and a
large slice are **done + verified live in the editor** (see `docs/HudSystem.md` for the
architecture and what shipped). This file lists what's **not** done, grouped by theme,
with the verification caveats that blocked finishing some items here.

## Done (for context — see HudSystem.md)
Retained custom-Vulkan `UISystem` HUD: data-driven JSON modules; widgets ProgressBar /
Repeater (vertical+horizontal) / Image (RGBA textures) / Label (word-wrap) / Button;
`bind` / `visibleWhen` / list bindings; engine-side per-frame binding; editor "Game view"
(renders into the offscreen viewport); **TTF fonts**; **engine default HUD shipped out of
the box** (`resources/ui/default_hud.json`); default modules **health / hotbar / objectives /
combat set**; combat HUD re-homed and the editor ImGui `renderCombatHUD` **deleted**;
**standard dialogue** migrated; **menus** migrated (editor path); UI **click-injection**
test hook (`/api/ui/click`).

---

## 1. Standalone host
The scaffold (the real shipped-game host) is now wired; **runtime verification still needs a
packaged/standalone build** (can't run a standalone in this environment).

- [x] **Scaffold (`tools/create_project.py`) wired** — generated game now calls
  `renderCoordinator_->initUISystem()`, registers HUD providers (player.health/maxHealth always;
  dialogue.* when a DialogueSystem exists), `UI::loadHudInto(...)` (engine default HUD), routes
  `onMenuSceneLoaded` → `UI::loadMenuInto` / `onSceneReady` → `UI::unloadMenuFrom`, and drives
  `UISystem::handleInput` for menu clicks. The ImGui `gameMenuRenderer_->render/load/unload` calls
  are gone. Verified by **generating** a project + reviewing the emitted C++ (engine helpers it
  calls are compiled+verified); **NOT compiled/run** — needs a packaged build.
- [ ] **Compile + run a packaged/standalone game** to verify the scaffold HUD/menus render and
  buttons click (the one remaining verification gap for "ships in a real game").
- [ ] **Scaffold cleanup**: `gameMenuRenderer_` is still declared/constructed (with its
  onTransitionScene/onQuit/onResolveVariable) but no longer renders — remove it once the standalone
  is verified. Also `{{token}}` interpolation (playtime/story.*) lived on `gameMenuRenderer_` — port
  to the HudDataContext text providers + the menu loader.
- [ ] **More scaffold providers**: hotbar/objectives/combat panels stay hidden (fail-closed) because
  the scaffold has no inventory/objectiveTracker/combat — wire them if/when those subsystems exist
  in the standalone.
- [ ] **`examples/minimal_game`** is **disabled in CMake** (re-enabling forces a full reconfigure —
  the ~38–48 min build hang; do it deliberately as a backgrounded targeted build if needed). Wire it
  as the compilable reference OR delete it; it still uses the ImGui `renderGameHUD`/ScreenState UI.
- [ ] **Standalone `GameMenuRenderer` in `EngineRuntime`**: `EngineRuntime` still owns/uses the ImGui
  `GameMenuRenderer` (its `getGameMenuRenderer()` + onMenuSceneLoaded wiring). Route via
  `UI::loadMenuInto` and remove it from the shipping path.
- [x] **Bundle assets into packaged games** (`tools/package_game.py`): `default_hud.json` added to
  REQUIRED_RESOURCES; the TTF font is already copied by the fonts step (§3b). So packaged games
  ship the HUD definition + font. (Copy mechanism verified by reading; not run end-to-end — full
  packaging needs a built game binary.)

## 2. Screens & remaining ImGui game UI
- [ ] **Intro / Victory / Credits screens** (`engine/src/ui/GameMenus.cpp`
  `renderIntro/Victory/CreditsScreen`, driven by `ScreenState`) → UISystem screens.
  Standalone-shell-driven; verify via a packaged run.
- [ ] **Countdown HUD** (`UI::renderCountdownHud`, ImGui) → a UISystem HUD module
  (Repeater/StatReadout bound to the timer trigger).
- [ ] **AI-conversation dialogue** still uses the ImGui `renderDialogueBox` (standard trees
  are migrated). Needs new UISystem widgets: a **scrollable container** + a **text-input
  field**. Until then AI chat stays on ImGui.

## 3. Menu feature parity (editor path works; polish missing)
`UI::loadMenuInto` covers background (solid/image), absolute layout, label/button/image,
button actions, and submenu panels. Not ported from `GameMenuRenderer`:
- [ ] Per-element **animations** (fade_in / slide_in_left / slide_in_right + delays).
- [ ] Per-element **custom fonts and text colors** (currently theme colors; needs UILabel
  custom color + per-widget font/size).
- [x] **`{{token}}` interpolation** in menu labels/buttons — DONE. `MenuActions.onResolveVariable`
  resolves `{{playtime}}` / `{{story.<var>}}` at menu load (static); unknown tokens left literal.
  Wired in the editor menu path + the `/api/ui/load_menu` debug hook. Verified live. (NOTE: static
  at load — a live-updating clock would need per-frame re-resolution; fine for credits/story vars.)
- [ ] `editor/MenuEditorPanel` still authors the `GameMenuRenderer` schema — fine (the loader
  consumes that schema), but eventually align authoring + preview on one path.

## 4. UISystem widget/feature gaps
- [ ] **UILabel custom color** (per-widget), used by menus/dialogue speaker.
- [ ] **Scrollable container** widget (AI chat, long option lists, quest logs).
- [ ] **Text-input** widget (AI chat, settings, name entry).
- [ ] **Themes**: extend `UITheme` to named themes loadable from JSON + a BG3 theme; per-game
  selection via `game.json` (`hud.theme` / `font`).
- [ ] **Per-module HUD merge/override**: `game.json "hud"` currently replaces the whole engine
  default (all-or-nothing); support overriding/adding individual modules.
- [ ] **Hotbar icons**: currently `material -> resources/textures/source/<lower>_top.png`;
  do proper item icons (atlas-tile UV from the voxel atlas, or per-item icon assets).
- [ ] Editor **"Game view" toggle**: hide editor chrome (World Outliner/Properties) for a true
  play-preview. Today the HUD just composites into the viewport.

## 5. Combat HUD S8 polish (deferred from TurnBasedCombat.md)
- [ ] d20 roll + crit/miss callouts; **floating damage numbers**.
- [ ] On-ground **movement-range ring + path spline** + target highlight — these are
  **world-space**, not 2D HUD; route via a VFX/debug-draw path, not `UIRenderer`.
- [ ] Initiative-row label clips at panel width (cosmetic); turn-label vs action-panel overlap
  in the default combat layout (cosmetic layout tuning).

## 5b. From game-dev feedback (docs/feedback/inbox.md, 2026-06-18, UIShowcase)
- [x] **Multi-scene games got no HUD in the editor** — `Application::autoLoadGameDefinition` (and the
  `load_game_definition` command handler) returned early in the multi-scene branch BEFORE the
  single-scene `setupGameHud()` + `combat.mode` setup, so multi-scene projects (menu→world) showed
  no HP bar / hotbar / objectives / combat and stayed `real_time`. FIXED: both multi-scene branches
  now apply `combat.mode` + call `setupGameHud()` before `transitionTo` (so HUD screens exist when a
  menu start-scene's `loadMenuInto` hides them). **Code compiles; runtime-verify pending an engine
  slot** (the build's exe-link was blocked by another session's running engine — see §6 multi-instance).
- [ ] **Menu `transition_scene` click didn't fire the scene transition** (open/close-submenu worked;
  "Back" went unresponsive after a couple clicks). The editor `onMenuSceneLoaded` does wire
  `transition_scene` → `SceneManager::transitionTo`, and the menu renders at boot, so suspect either
  the injected click landed on the panel not the button, or queued `ui_click`s racing the scene pump.
  Investigate live (reproduce the New Game click in UIShowcase) when an engine slot is free.

## 6. Known issues / cleanup
- **Multi-instance / ports:** multiple engines run at once, each on its OWN `api_port` (default 8090
  = engine-dev). `create_project.py` now assigns each project a unique `engine.json api_port` +
  a `.mcp.json` with matching `PHYXEL_API_PORT`. NEVER `taskkill //IM phyxel.exe` (kills other
  sessions); the shared `phyxel.exe` can't relink while any instance runs it (LNK1104) — don't kill
  others to unblock a build. (Existing projects like UIShowcase predate this and use 8090; add an
  `api_port` to their engine.json when convenient.)
- [ ] **Pre-existing scene-transition crash**: multi-scene `load_game_definition` /
  scene transitions intermittently crash (`vulkan-1.dll`, predates this work — see
  `AgentContext.md`). Flag for separate triage. (Menus were verified via the direct
  `/api/ui/load_menu` hook to avoid it.)
- [ ] `BitmapFont::initializeTTF` log uses a `{:.3f}` format spec the logger prints literally —
  trivial; switch to `{}`.
- [ ] `main` has 9 pre-existing failing unit tests (material/atlas counts, inventory, skeleton
  hinge, nav StepUp) unrelated to the HUD — separate triage (noted in AgentContext).

## Verification notes (how to test HUD work here)
- Launch directly (NOT MCP `launch_engine` — it deadlocks): `phyxel.exe --project <CharacterTestbed|DebrisPushTest full path>`, drive via HTTP `localhost:8090`.
- HUD/combat: `DebrisPushTest` (flat world). Dialogue: `start_dialogue`. Menus: `POST
  /api/ui/load_menu {layout}` (direct, avoids the scene-transition crash). Buttons:
  `POST /api/ui/click {x,y}` (UI-space = window resolution).
