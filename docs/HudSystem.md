# Game-HUD System — Design

> Status: **implemented + merged to `main`** (data-driven HUD on the custom Vulkan `UISystem`;
> default HUD shipped, combat HUD re-homed off ImGui). This doc is the architecture; remaining
> work is in the "Open Items / Remaining Work" section at the end. First customer + the former
> ImGui stopgap it replaced: `docs/TurnBasedCombat.md` → S8.

## 1. Goal

A real engine subsystem for **game HUDs** that game developers author against:

- A **default HUD** the engine ships (health, hotbar, objectives, dialogue, combat widgets).
- **Rich, data-driven customization** so devs reposition / restyle / replace / theme it —
  "defaults are fine, customization makes it pop."
- A faithful story for **editor preview vs. shipped game** — the HUD a dev sees in the editor is
  the HUD that ships.

## 2. Decisions (resolved with the user)

| Fork | Decision |
|------|----------|
| **Foundation / rendering backend** | **Extend `UISystem`** — the existing retained-mode, custom-Vulkan widget tree. NOT ImGui. |
| **Authoring model** | **Data-driven JSON** (primary, in `game.json` / assets) **+ a code/scripting escape hatch** for bespoke widgets. |
| **Editor preview** | **Both** — a play/"Game view" (HUD composited over the viewport, editor chrome hidden) *and* a dedicated HUD-preview/authoring panel. |
| **Target look** | **BG3 / D&D-RPG style** default theme (portraits, ornate frames, dice/combat readouts) — aligns with the turn-based combat first customer. |

Two remaining forks (proposed below, not blockers): **data binding** (§6) and **v1 module +
theming scope** (§7).

### 2a. Guiding principle — NO ImGui in a shipped game (user, firm)

**The final deployable game must contain ZERO ImGui for real UI** — menus, screens, HUD,
dialogue, all of it renders through the custom-Vulkan `UISystem`. ImGui stays as the **editor
app's** primary UI, and the *only* permitted ImGui in a packaged build is an **optional backend
debug overlay that can be stripped/compiled out** of release games. Rationale: ImGui doesn't hold
up for a finalized, stylized game.

Consequence: this is not just "build the HUD on `UISystem`." The game-facing UI that ships *today*
is mostly ImGui, so the HUD work comes with a committed **migration of all shipped UI off ImGui**
(§11a). The `UISystem` foundation choice is what makes that reachable.

## 3. Why `UISystem` (the grounded reality)

The engine already has **three** UI paths; two are data-driven *shipping* game-UI systems. The
HUD kickoff doc only saw one of them. Verified in code:

| Path | Backend | Authoring | Renders | Ships | Visible in editor viewport |
|------|---------|-----------|---------|-------|----------------------------|
| `ImGuiRenderer` (`engine/src/ui/ImGuiRenderer.cpp`) | ImGui windows | code | editor pass | ❌ | yes (editor only) — hosts the **misplaced `renderCombatHUD`** |
| `GameMenuRenderer` (`engine/src/ui/GameMenuRenderer.cpp`, owned by `EngineRuntime`) | ImGui `ImDrawList` | **JSON** (`SceneDefinition.menuLayout`) | foreground draw list | ✅ | yes (`renderToForeground_` hack draws over the dockspace) |
| **`UISystem` + `MenuDefinition`** (`engine/src/ui/UISystem.cpp`, owned by `RenderCoordinator`) | **custom Vulkan** retained widget tree | **JSON** (`MenuDefinition`) | **post-process / swapchain pass** | ✅ | **no** (post-process only shows in packaged builds) |

`UISystem` already provides the HUD bones:

- **Retained widget tree** (`UIWidget` base; `UIPanel/UILabel/UIButton/UISlider/UICheckbox/
  UIDropdown/UIImage`) with anchor-based layout (`Anchor` TopLeft…BottomRight + offset).
- **Theming** (`UITheme`: colors + sizing) — the seed for named/JSON themes.
- **Data-driven loading** (`MenuDefinition::buildFromJson`) and serialization (`toJson`).
- **Batched custom-Vulkan rendering** in one draw call (`UIRenderer`, R8 font atlas) — no ImGui in
  the shipped HUD; full perf/theming control.
- Already wired: created in `RenderCoordinator::initUISystem()`, rendered in the post-process pass
  (`m_uiSystem->render`, GPU scope "Custom UI"), input via `handleInput`, MCP add/show/toggle-screen
  control, and an editor authoring panel (`MenuEditorPanel`).

So we are **extending a system that already ships and is themeable**, not inventing a renderer.

### The one real gap: editor preview

`UISystem` renders in the **post-process pass** (swapchain target). The editor viewport shows the
**offscreen scene image** (`RenderCoordinator::getViewportImageView()` → `ImGui::Image` in
`Application::renderDockableViewport`), sampled *before* post-process. **So the retained HUD is
invisible in the editor today.** Fixing this is the core editor-preview task (§5).

### Consolidation note (committed direction — see §11a)

Per the no-ImGui principle (§2a), `GameMenuRenderer` (ImGui menus), the `GameMenus.cpp` ImGui
screens, and the ImGui countdown HUD are **migration targets, not foundations** — they must move
onto `UISystem`. The end state is *one* game-UI system (`UISystem`) with ImGui editor-only. The
features `UISystem` must absorb to fully replace them: per-element **animations** (fade/slide),
**TTF fonts** (required for the BG3 look — `UIRenderer` is R8 bitmap today), `{{token}}`
**interpolation** (becomes §6 bindings), and **MCP live-control**. Do it incrementally (user pref:
avoid big rewrites) — the HUD vertical slice (§9) builds the core, then §11a migrates each surface.

## 4. Architecture

```
game.json ─ "hud": { themes, modules[] }      (+ per-scene override)
    │
    ▼
HudDefinition (JSON → widget tree)  ── reuses/extends MenuDefinition
    │
    ▼
HudSystem (new thin owner)
    ├── widget tree (UISystem widgets + new HUD widgets §6)
    ├── HudDataContext (binding registry §6)   ← reads live game state (single source of truth)
    └── UITheme (named themes §7)
    │
    ▼
UIRenderer (custom Vulkan, batched)
    ├── post-process pass  → swapchain  (shipped game)
    └── offscreen viewport pass → editor "Game view"  (§5, new)
```

`HudSystem` is a **thin module on top of `UISystem`**, not a parallel renderer. It owns the HUD
widget tree, the data-binding context, and theme selection; it delegates drawing to the existing
`UIRenderer`. HUD widgets are normal `UIWidget`s so menus and HUD share one renderer/theme/layout.

- **Ownership:** `RenderCoordinator` already owns `UISystem`; `HudSystem` lives alongside (or as a
  member of `UISystem`). The standalone shell (`GameShell`) and the editor both drive it, mirroring
  how `GameMenuRenderer` is shared (the camera/control precedent: one abstraction, two hosts —
  `docs/CameraControlSystem.md`).
- **Single source of truth (user pref):** HUD widgets **read** state through `HudDataContext`
  providers; they never store gameplay state. Update is pull-per-frame (cheap; see §8).

## 5. Editor preview ("Game view" + panel)

**5a. Game view (in-scene fidelity).** Make the retained HUD render into the **offscreen viewport
color target** the editor samples — i.e. the same `UIRenderer` draws into both the post-process
(swapchain, shipped) pass and the offscreen pass that feeds `getViewportImageView()`. Then the
editor viewport shows the literal shipping HUD. A "Play / Game view" toggle hides editor chrome
(World Outliner / Properties) so the dev previews exactly what ships. This *also* structurally
closes the post-process-parity gap noted in `docs/AgentContext.md`.

- Implementation choice to settle at build time: (a) add a UI sub-pass to the offscreen render pass
  and draw the HUD there too, vs. (b) draw the HUD as a textured overlay into the viewport image
  rect. (a) is true parity (same pixels both places); (b) is cheaper but risks editor-only drift.
  **Lean (a).**

**5b. HUD-preview / authoring panel (fast iteration).** A dedicated editor panel (mirror
`MenuEditorPanel`) that renders a HUD definition in isolation with **mock data** (toggleable to
live data), for laying out/restyling modules without running a scene. Honors the editor-UI
conventions (action buttons always visible; per-module props on the module's panel; global theme
settings on their own panel).

## 6. Data binding (proposed)

Generalize `GameMenuRenderer`'s `{{token}}` resolver into a typed **`HudDataContext`**: a registry
of named providers the host wires once; widgets reference them by binding key.

- **Binding key on widgets:** e.g. `"bind": "player.health"`, `"bind": "combat.turn_order"`.
- **Provider signature (typed, read-only):** returns `float` / `int` / `string` / `bool` /
  `list<record>` for a key. Hosts register providers from live systems:

| Binding namespace | Source system |
|-------------------|---------------|
| `player.health`, `player.maxHealth`, `player.resource.*` | `HealthComponent` / shared `playerHealth` |
| `player.hotbar[*]`, `player.selectedSlot` | hotbar / `EquipmentSystem` |
| `combat.inCombat`, `combat.round`, `combat.isPlayerTurn`, `combat.currentEntityId` | `CombatDirector` |
| `combat.turn_order` (list of portrait records) | `CombatDirector` / `InitiativeTracker` |
| `combat.action`, `combat.bonus`, `combat.movement` | `ActionBudget` (via `PlayerTurnController`) |
| `combat.hitChance`, `combat.selectedTarget` | `PlayerTurnController` |
| `objectives[*]` | `ObjectiveTracker` |
| `story.<var>` | `StoryEngine` WorldState (already exposed via `onResolveVariable`) |
| custom | host-registered provider (the code escape hatch) |

- **Text** still supports `{{token}}` interpolation (reuse the existing path, backed by the same
  context). **Bars/lists** consume typed values directly.
- **List binding** drives a new **repeater** widget (one templated child per record) — this is how
  the initiative portrait bar and the hotbar are built without hardcoding counts.

## 7. Widgets, default modules & theming (proposed)

**New HUD widget types** (added to the `UISystem` widget set):

- `ProgressBar` — value/min/max + fill/track colors (health, resource bars).
- `Icon` — atlas/PNG sprite (status icons, slot contents, dice faces).
- `Repeater` — templated children bound to a `list` provider (turn order, hotbar, objectives).
- `StatReadout` — label + bound value (round counter, action budget).

**v1 default modules** (composable; each reposition/restyle/replace/hide-able):

1. ✅ **Health bar** (the vertical-slice widget §9) — DONE.
2. ✅ **Hotbar** — DONE + verified live. **Horizontal** Repeater over a `hotbar` list provider
   (first 9 inventory slots); each slot = a panel with a `UIImage` icon (the material's
   `_top.png` source texture) + count label; the selected slot's icon is full-bright, others
   dimmed (live via `getSelectedSlot()`).
3. ✅ **Objectives tracker** (Repeater over `objectives`, `[x]`/`[ ]` markers, gated by
   `objectives.any`) — DONE + verified live; second Repeater customer (proves it generalizes).
4. ✅ **Combat set** (§10) — turn-order **list** (Repeater over `combat.turn_order`), **action
   bar** (Action/Bonus/Movement + **End Turn** button), **hit-chance readout** — DONE.

**`UIImage` arbitrary textures — DONE.** `UIRenderer` loads PNGs into per-texture descriptor sets
(`loadTexture`, cached by path) and batches by texture into draw runs; a `mode` push-constant
switches the fragment shader between R8 alpha-mask (text/rects) and RGBA×tint (images). Repeater
items can bind an image path per record (`item.icon`). `UIRepeater.horizontal` lays items in a row.

✅ **Shipped default HUD — DONE.** The engine ships `resources/ui/default_hud.json` (all the modules
above); `Application::setupGameHud` loads it whenever a game's `game.json` has no `"hud"` key, so a
game gets the full HUD **with zero authoring**. A game's own `"hud"` (object or array) overrides it.
Verified live: DebrisPushTest with no `hud` block shows HP + hotbar (combat/objectives panels
hidden until relevant). Follow-up: per-module merge/override (vs all-or-nothing), and bundling
`resources/ui/` + the font into packaged games.

Dialogue stays in `DialogueSystem` for now (its own system); fold under the HUD later.

**Theming (v1 scope):** extend `UITheme` into **named themes loadable from JSON**; ship a
**BG3-style default theme** (warm parchment/gold palette, framed panels, TTF font for headings).
Per-module overrides (position/anchor/colors/visibility). **Animations deferred** to a later pass
(port `GameMenuRenderer`'s fade/slide when consolidating).

## 8. Performance (first-class constraint)

- HUD draws every frame but is a **handful of batched quads** through the existing `UIRenderer`
  (one draw call). Keep binding reads O(widgets); no per-frame allocations in the pull path.
- Measure with the standing per-pass endpoints (`/api/debug/gpu_scopes` "Custom UI",
  `frame_profile`) — grade against the frame budget. Debug numbers are not shippable.
- List/repeater providers should hand back cached vectors, not rebuild per frame.

## 9. First vertical slice — ✅ DONE & verified live (2026-06-17)

1. ✅ **`HudDataContext`** (`engine/include/ui/HudDataContext.h`, header-only) — typed
   float/text provider registry; `applyBindings(root)` walks the widget tree and pulls live
   values into widgets via their `bind` key (widgets stay dumb; provider = source of truth).
   Providers registered in `Application::setupGameHud`: `player.health` / `player.maxHealth` /
   `player.healthText` (read the shared `playerHealth`).
2. ✅ **`UIProgressBar`** widget (`UIWidget.h/.cpp`, `WidgetType::ProgressBar`) — drawRect-based
   fill bar (value/min/max + fill/track/border colors + centered "cur/max" text). No texture
   work needed. `MenuDefinition` parses `"progressbar"`/`"bar"` + a `"bind"` field on bindable
   widgets (+ toJson case).
3. ✅ **HUD JSON load** — `Application::setupGameHud(gameDef)` reads the top-level `game.json`
   `"hud"` block (a `MenuDefinition` panel tree), `addScreen("hud")` + `showScreen("hud")`;
   `applyBindings` runs each frame in `Application::render()`. Demo block in
   `PhyxelProjects/DebrisPushTest/game.json` (BottomLeft health bar bound to `player.health`).
4. ✅ **Editor Game-view** — `UISystem` now initializes against the **scene render pass** and
   renders **last in the scene pass into the offscreen image** (was: post-process/swapchain).
   So it shows in the editor Viewport panel (which samples the offscreen image) AND is carried
   to the swapchain by post-process for standalone. One pipeline; no ImGui. (`RenderCoordinator`.)
5. ✅ **Verified live** — built clean; launched DebrisPushTest; the bar renders bottom-left in
   the viewport reading "HP 100/100"; `damage_player 62` → bar shrinks to ~38% reading "HP
   38/100". Data binding + offscreen render both confirmed by screenshot.

**Slice gotchas / notes for next time:**
- The HUD is baked into the offscreen image, so in a shipped build it passes through
  post-process. Fine now (post-process is a passthrough composite) — revisit if tonemap/bloom
  are re-enabled (UI should not be tonemapped; would need a post-scene UI pass).
- `HudDataContext` is **editor-side wired only** (`Application`). Standalone host (`GameShell` /
  `minimal_game`) still needs the same provider registration + per-frame `applyBindings` — part
  of §11a.
- ProgressBar `max` is authored (100), not bound; bind `player.maxHealth` too if max can change.
- No "Game view" chrome-hiding toggle yet — the HUD simply shows in the viewport. A real
  play-mode toggle (hide World Outliner/Properties) is a follow-up.

Next: re-home the combat HUD (§10) and/or the §11a ImGui migration.

## 10. First real customer — re-home the combat HUD

Re-express the stopgap `renderCombatHUD` (raw ImGui in the editor `ImGuiRenderer`) as **default HUD
modules** bound via §6:

- ✅ **Round banner** + **turn label** + **action bar** (Action/Bonus/Movement) + **hit-chance**
  readout — DONE (`da5948c`). Data from `CombatDirector` / `PlayerTurnController` via `combat.*`
  providers; visibility gated by `visibleWhen` (`combat.inCombat` / `combat.playerTurnActive`).
  Verified live + parity with the old ImGui HUD.
- ✅ Turn-order **list** (Repeater over a `combat.turn_order` list provider) — DONE (`bd6cf5c`).
  New `UIRepeater` widget + `HudRecord`/`ListProvider`; "Initiative" panel verified live
  (`> player [20] …`, `npc_goblin [8]`, active marker).
- ✅ **End Turn button** — DONE, data-driven `UIButton` → `PlayerTurnController::endTurn()`.
  **Click verified live** via the new click-injection feature (below): clicking it advanced the
  round 1→2.
- Planned S8 pieces: d20 roll + crit/miss callouts, floating damage numbers, on-ground
  movement-range ring + path spline, target highlight.

✅ **`renderCombatHUD` DELETED** (definition + declaration + call site) — the combat HUD is now
fully data-driven on `UISystem`. Verified live post-deletion: combat HUD renders, the old ImGui
combat HUD is gone, End Turn still advances the round.

**Click-injection test feature** (so agents can verify interactive HUD/menu widgets without a real
mouse): `UISystem::injectClick(pos)` routes a synthetic click through the same per-screen
hit-testing as `handleInput`; `POST /api/ui/click {x,y}` → `ui_click` handler → returns
`{consumed}`. **Authoring gotcha found via this:** `UIPanel::handleClick` first hit-tests the
panel's OWN rect, so a child that overflows below a too-short panel is unclickable — **size HUD
panels to contain their children.** Cosmetic leftovers: the initiative row label clips at panel
width; the turn-label panel overlaps the (enlarged) action panel — both layout-tuning only.

## 11a. Migration roadmap — get ImGui out of shipped games (§2a)

The packaged game must end up ImGui-free for real UI. Surfaces to migrate from ImGui onto
`UISystem` (do incrementally, after the §9 slice proves the foundation):

| Surface | Today | Target |
|---------|-------|--------|
| **Menus** (main/options/etc.) | ✅ DONE in the EDITOR — `UI::loadMenuInto` converts the GameMenuRenderer schema (1280x720 positions, solid/image bg, label/button/image, button actions, submenu panels) into UISystem fullscreen "menu:*" screens; new UISystem caps: widget `position` + panel `freeLayout` (absolute layout) + fullscreen bg. Editor `onMenuSceneLoaded` routes to it; the ImGui `GameMenuRenderer->render` is removed from the editor loop. Verified live (PHYXEL DEMO menu: title/buttons render, Options → submenu nav works) via the `/api/ui/load_menu` test hook. **Not yet ported:** per-element animations (fade/slide), custom fonts/colors, {{token}} interpolation. **Standalone host (EngineRuntime) still uses `GameMenuRenderer`** — migrate with the standalone-host work (verify via packaged run). NOTE: multi-scene `load_game_definition` hit the **pre-existing scene-transition crash** (unrelated to menus). |
| **Pause menu** | ✅ DONE — `UI::loadPauseMenuInto`/`unloadPauseMenuFrom` build a data-driven `pause:*` UISystem overlay from `resources/ui/pause_menu.json` (dark scrim + PAUSED + Resume/Settings/Main Menu/Quit), wired via new `MenuActions::onResume/onSettings/onMainMenu` + button-action types `resume`/`open_settings`/`main_menu`. The scaffold (`create_project.py`) loads/unloads it to match `ScreenState::Paused` and drives `handleInput`; the ImGui `renderPauseMenu` call is gone. Verified live on the `R5Verify` standalone (ESC → styled overlay, Resume → gameplay). **HUD-suppression-while-paused (#11) DONE:** `loadPauseMenuInto` hides every non-`pause:*` screen and `unloadPauseMenuFrom` restores them, and the scaffold only queues world labels (bubbles/prompt) while `Playing` — verified live (the top-center countdown HUD vanishes when paused, returns on resume). **Follow-ups:** Settings/Main Menu still route through their own (now data-driven) screens; editor still uses ImGui `renderPauseMenu`. |
| **Screens** (Intro/Victory/Credits) | ✅ DONE — `UI::loadGameScreenInto(ui,"intro"\|"victory"\|"credits",…)` builds a full-screen data-driven `<name>:*` overlay from `resources/ui/<name>_screen.json` (shared `loadOverlayFromFile` helper with the pause menu). Dynamic text via `{{title}}`/`{{tagline}}` tokens (`MenuActions.onResolveVariable`); buttons use `main_menu`/`show_credits`/`quit_game` (+ new `MenuActions::onShowCredits`). The scaffold's one reconcile loop maps `ScreenState`→overlay (pause/intro/victory/credits), (un)loads on change, and drives `handleInput`; the ImGui `renderIntro/Victory/CreditsScreen` calls are gone. Verified live on `R5Verify` (jump→`show_victory` trigger → VICTORY! overlay with resolved title → Credits → resolved title+tagline). **Follow-ups:** intro's old any-key-continue dropped (Continue button only); the fallback `renderMainMenu` + `renderSettingsScreen` are still ImGui (next slices); editor's ImGui screens untouched. |
| **Countdown HUD** | ✅ DONE — `hud_countdown` panel in `default_hud.json` (top-center, `isTitle`), gated by `visibleWhen: "countdown.active"`, text bound to `countdown.text`. The host registers both providers from `TriggerSystem::getActiveCountdowns()` (label + `M:SS.s` of the first active countdown). Scaffold no longer calls the ImGui `renderCountdownHud`. **Follow-ups:** single countdown only (no Repeater for multiple); no red-under-10s urgency color (HUD labels lack a color bind); the EDITOR still uses ImGui `renderCountdownHud`. |
| **Dialogue** | ✅ DONE — trees AND AI conversations. Trees: `hud_dialogue` panel (speaker + word-wrapped text + numbered choices). **AI conversations: `hud_ai_dialogue` panel** (speaker + scrollback history + a **`UITextInput` field**), wired by `UI::setupAIDialogue` (dialogue.aiActive/aiSpeaker/aiHistory providers + the field's submit → `DialogueSystem::submitPlayerMessage`). This required NEW engine infra: GLFW **char capture** (`WindowManager` char callback, re-owned after ImGui steals it in `reinstallScrollCallback`, → `InputManager::handleChar`/`getTypedChars`) and a **`UITextInput` widget** (UISystem routes typed chars + Backspace/Enter to the focused field). The ImGui `renderDialogueBox` is **GONE** from both scaffold and editor. Verified live (editor, project-open + world scene + active AI conversation): box renders with the NPC greeting, the field auto-focuses, and typed characters echo into it. **Follow-ups:** long input overflows the field (no horizontal clip/scroll); em-dash in placeholders renders as `?` (bitmap font glyph gap). |
| **Settings** | ✅ DONE — data-driven `settings:*` overlay (`loadGameScreenInto(ui,"settings",…)` from `resources/ui/settings_screen.json`); replaces ImGui `renderSettingsScreen`. Standard **Graphics** (Resolution/V-Sync/Fullscreen/FOV) / **Audio** (Master/Music/SFX) / **Controls** (Mouse Sensitivity). `buildMenuElement` now builds slider/checkbox/dropdown bound BIDIRECTIONALLY to `GameSettings` via new `MenuActions::onGetSetting`/`onSetSetting` (floats; checkbox=0/1, dropdown=index); `back` action via `onBack`. Changes apply live; host saves on Back. **Now two columns with the previously-deferred rows added:** **Brightness** (→ `RenderCoordinator::setAmbientLightStrength`, the engine's "brightness multiplier"), **Invert Y** (→ new `InputManager::setInvertY`, flips `mouseDeltaY`), and an **AI** section — Provider dropdown + **Model / API-Key `textinput` fields** bound to STRING settings via new `MenuActions::onGetSettingText`/`onSetSettingText` (`buildMenuElement` gained a `textinput` type) → `GameSettings` + `AIConversationService::setLLMConfig`. Verified live (widgets show current values; FOV slider applied; Back→pause). **Keybind rebind SHIPPED** (`ef498d3`/`1e2ed9c`, 2026-06-20): a data-driven "Keybindings…" sub-panel (one row per default action) drives a one-shot key-capture flow (`UISystem::beginKeyCapture`) into `InputManager::bindAction`, which now reads/rebinds the live action map (no longer hardcoded); `MenuActions::onRebindKey` persists to `GameSettings` and refreshes the row label. **Polish:** API-key field isn't masked; long text overflows the field; open-dropdown overlap. |
| **Fallback main menu** | ✅ DONE — data-driven `mainmenu:*` overlay (`loadGameScreenInto(ui,"mainmenu",…)` from `resources/ui/mainmenu_screen.json`, `{{title}}` + New Game/Options/Quit); replaces ImGui `renderMainMenu`. New `MenuActions::onStartGame` + `start_game` action. The scaffold reconcile maps `ScreenState::MainMenu`→it; games with a `sceneType:"menu"` start scene render that menu scene instead. Verified live (pause→Main Menu shows the data-driven title screen). With this, **ALL `ScreenState` screens (Intro/MainMenu/Victory/Credits/Settings/Paused) are off ImGui.** |
| **Speech bubbles + `[E]` interaction prompt** | ✅ DONE — world-anchored text via new `UISystem::worldToScreen` (project a world pos to screen px) + `UISystem::addWorldLabel` (per-frame imperative label drawn in `render()` after the retained screens: centered, box above the point, then cleared). The scaffold projects each `SpeechBubbleManager` bubble + the `InteractionManager` nearest-NPC position and queues labels; the ImGui `renderSpeechBubbles`/`renderInteractionPrompt` calls are gone. Verified live on `R5Verify` (the "Interact" prompt renders above Elder Maewyn). **Follow-ups:** no rounded corners / pulse animation; speech bubble had no easy standalone trigger to screenshot (same `worldToScreen`+`addWorldLabel` path as the verified prompt); editor still ImGui. **MILESTONE: the generated scaffold now has ZERO ImGui gameplay render calls except the AI-conversation dialogue box** (kept intentionally — UISystem lacks scroll + text-input widgets). |
| **Standalone host** | `examples/minimal_game/MinimalGame.cpp` + the scaffold (`tools/create_project.py`) call `imgui->newFrame/endFrame` each frame and delegate to `GameMenuRenderer` | drive `UISystem`; ImGui only behind a debug flag |
| **Combat HUD** | `renderCombatHUD` (editor ImGui) | default HUD modules (§10); deleted from `ImGuiRenderer` |

End state: a packaged game does **not initialize ImGui for UI** — only an optional, compile-out
**debug overlay** may use it. ImGui remains the editor app's primary UI.

## 11b. Open items / risks

- Editor Game-view render-target choice (§5a (a) vs (b)) — settle at implementation.
- ✅ **TTF font support — DONE.** `BitmapFont::initializeTTF` bakes a TrueType font (stb_truetype)
  into the R8 atlas (anti-aliased glyphs reuse the mode-0 alpha-mask path); metrics are normalized
  so a line is `GLYPH_H` px at scale 1.0 → **no layout shift** when swapping from the bitmap font.
  `UISystem` prefers `resources/fonts/JetBrainsMonoNerdFontMono-Regular.ttf`, falls back to the
  bitmap font. The reserved white texel (for `drawRect`) moved into the TTF atlas; `UIRenderer::
  setWhitePixelUV` makes its location font-controlled. Verified live (crisp HUD text). Follow-ups:
  per-font selection / multiple sizes from `game.json`; bundle the font in packaged games.
- Hotbar icons currently map `material -> resources/textures/source/<lower>_top.png` (works for
  materials with a top texture; others fall back to a placeholder rect). A proper item-icon source
  (atlas-tile UV, or per-item icon assets) is a follow-up; UIImage already supports the texture.
- `UISystem` must absorb **animations** (fade/slide) and **MCP live-control** to replace
  `GameMenuRenderer` at parity — schedule with §11a, not v1.
- On-ground combat decorations (movement ring, path spline) are **world-space**, not 2D HUD — they
  belong with a VFX/debug-draw path, not `UIRenderer`; scope them separately.
- Don't add new game-facing features to `GameMenuRenderer`/`GameMenus` ImGui paths — it widens the
  migration gap (§11a).
```

## Open Items / Remaining Work

> Merged from the former `HudSystem_TODO.md` (status as of the `feature/hud-system` merge,
> 2026-06-17). The architecture and a large slice are done + verified live in the editor (above);
> these are the items **not** done, plus the verification caveats that blocked finishing some here.
> (Items already marked ✅ in §7/§10/§11a above are not repeated.)

### 1. Standalone host
The scaffold (the real shipped-game host) is now wired; **runtime verification still needs a
packaged/standalone build** (can't run a standalone in this environment).

- [x] **Scaffold (`tools/create_project.py`) wired** — generated game calls
  `renderCoordinator_->initUISystem()`, registers HUD providers (player.health/maxHealth always;
  dialogue.* when a DialogueSystem exists), `UI::loadHudInto(...)` (engine default HUD), routes
  `onMenuSceneLoaded` → `UI::loadMenuInto` / `onSceneReady` → `UI::unloadMenuFrom`, and drives
  `UISystem::handleInput` for menu clicks. The ImGui `gameMenuRenderer_->render/load/unload` calls
  are gone. Verified by **generating** a project + reviewing emitted C++ (engine helpers it calls
  are compiled+verified); **NOT compiled/run** — needs a packaged build.
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
  REQUIRED_RESOURCES; the TTF font is already copied by the fonts step. So packaged games ship the
  HUD definition + font. (Copy mechanism verified by reading; not run end-to-end — full packaging
  needs a built game binary.)

### 2. Screens & remaining ImGui game UI
- [ ] **Intro / Victory / Credits screens** (`engine/src/ui/GameMenus.cpp`
  `renderIntro/Victory/CreditsScreen`, driven by `ScreenState`) → UISystem screens.
  Standalone-shell-driven; verify via a packaged run. (NOTE: editor path migrated per §11a; the
  remaining gap is standalone verification.)
- [ ] **Countdown HUD** (`UI::renderCountdownHud`, ImGui) → a UISystem HUD module
  (Repeater/StatReadout bound to the timer trigger). (Editor path migrated per §11a; standalone open.)
- [ ] **AI-conversation dialogue** — needs scrollable-container + text-input widgets to fully leave
  ImGui in the standalone (editor path migrated per §11a).

### 3. Menu feature parity (editor path works; polish missing)
`UI::loadMenuInto` covers background (solid/image), absolute layout, label/button/image,
button actions, and submenu panels. Not ported from `GameMenuRenderer`:
- [x] Per-element **animations** (fade_in / slide_in_left / slide_in_right / slide_in_up +
  delays) — DONE 2026-08-19 on UISystem widgets (same JSON schema as the old renderer;
  UIWidget::computeAppear + UIRenderer anim stack + per-screen show timestamps; replays on
  every screen show). Pixel-verified early-vs-settled on Hearthvale.
- [x] **Named themes** — DONE 2026-08-19: `"theme"` key on menuLayout/screen JSONs (preset
  name from `resources/ui/themes/` — slate/ember/parchment — or inline key overrides),
  applied to the global UITheme via `MenuDefinition::applyTheme`.
- [x] Per-element **custom colors + text scale** — DONE 2026-08-19: labels take `"color"`
  ([r,g,b(,a)] 0-1) + `"scale"` (absolute font scale; body=2, title=3), buttons take
  `"color"` (text) / `"bg"` / `"bgHover"` (unset hover = bg +25%); parsed on BOTH build
  paths, alpha-0 sentinel = theme. Pixel-verified on Hearthvale (Quit bg vs Begin bg,
  subtitle glyph cores R-B 17→54). Custom FONTS (different typefaces) remain open — one
  bitmap font today.
- [x] **`{{token}}` interpolation** in menu labels/buttons — DONE. `MenuActions.onResolveVariable`
  resolves `{{playtime}}` / `{{story.<var>}}` at menu load (static); unknown tokens left literal.
  Verified live. (NOTE: static at load — a live-updating clock would need per-frame re-resolution.)
- [ ] `editor/MenuEditorPanel` still authors the `GameMenuRenderer` schema — fine (the loader
  consumes that schema), but eventually align authoring + preview on one path.

### 4. UISystem widget/feature gaps
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

### 5. Combat HUD S8 polish (deferred from TurnBasedCombat.md)
- [ ] d20 roll + crit/miss callouts; **floating damage numbers**.
- [ ] On-ground **movement-range ring + path spline** + target highlight — these are
  **world-space**, not 2D HUD; route via a VFX/debug-draw path, not `UIRenderer`.
- [ ] Initiative-row label clips at panel width (cosmetic); turn-label vs action-panel overlap
  in the default combat layout (cosmetic layout tuning).

### 5b. From game-dev feedback (docs/feedback/inbox.md, 2026-06-18, UIShowcase)
- [x] **Multi-scene games got no HUD in the editor** — `Application::autoLoadGameDefinition` (and the
  `load_game_definition` handler) returned early in the multi-scene branch BEFORE the single-scene
  `setupGameHud()` + `combat.mode` setup, so multi-scene projects (menu→world) showed no HUD and
  stayed `real_time`. FIXED: both multi-scene branches now apply `combat.mode` + call `setupGameHud()`
  before `transitionTo`. **Code compiles; runtime-verify pending an engine slot.**
- [x] **Menu `transition_scene` click didn't fire the scene transition (the Back soft-lock) — FIXED**
  (`161e006`, "snapshot visible screens before click dispatch"). Root cause: `UISystem::handleInput`/
  `injectClick` delivered one click to every screen visible in the iteration, so a button's `onClick`
  that revealed another screen mid-loop (e.g. `close_submenu`) let the newly-revealed screen receive
  the SAME click and instantly re-fire whatever sat under the cursor (Credits→Back bounced back to
  Credits). Both methods now snapshot `visibleScreenSnapshot()` before dispatch.

### 6. Known issues / cleanup
- **Multi-instance / ports:** multiple engines run at once, each on its OWN `api_port` (default 8090
  = engine-dev). `create_project.py` now assigns each project a unique `engine.json api_port` +
  a `.mcp.json` with matching `PHYXEL_API_PORT`. NEVER `taskkill //IM phyxel.exe` (kills other
  sessions); the shared `phyxel.exe` can't relink while any instance runs it (LNK1104).
- [ ] **Pre-existing scene-transition crash**: multi-scene `load_game_definition` / scene transitions
  intermittently crash (`vulkan-1.dll`, predates this work — see `AgentContext.md`). Flag for
  separate triage. (Menus were verified via the direct `/api/ui/load_menu` hook to avoid it.)
- [ ] `BitmapFont::initializeTTF` log uses a `{:.3f}` format spec the logger prints literally —
  trivial; switch to `{}`.
- [ ] `main` has 9 pre-existing failing unit tests (material/atlas counts, inventory, skeleton
  hinge, nav StepUp) unrelated to the HUD — separate triage (noted in AgentContext).

### Verification notes (how to test HUD work here)
- Launch directly (NOT MCP `launch_engine` — it deadlocks): `phyxel.exe --project <CharacterTestbed|DebrisPushTest full path>`, drive via HTTP `localhost:8090`.
- HUD/combat: `DebrisPushTest` (flat world). Dialogue: `start_dialogue`. Menus: `POST
  /api/ui/load_menu {layout}` (direct, avoids the scene-transition crash). Buttons:
  `POST /api/ui/click {x,y}` (UI-space = window resolution).
