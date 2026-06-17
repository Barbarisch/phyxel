# Game-HUD System — Design

> Status: **design agreed, not yet built** (2026-06-17). This doc is the architecture the
> implementation follows. Kickoff context: `docs/HUD_NEXT_SESSION.md`. First customer + the
> stopgap being replaced: `docs/TurnBasedCombat.md` → S8.

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

Note: these are currently authored per-game in `game.json` (demo set in
`PhyxelProjects/DebrisPushTest/game.json`). A **shipped default HUD** auto-injected by the engine
(so games get it without authoring) is a follow-up.

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
| **Menus** (main/options/etc.) | `GameMenuRenderer` (ImGui `ImDrawList`), the live menu path | `UISystem` menu definitions (already its original purpose; `MenuDefinition` JSON) |
| **Screens** (Intro/Victory/Credits) | `GameMenus.cpp` `renderIntro/Victory/CreditsScreen` (ImGui) | `UISystem` screens driven by `ScreenState` |
| **Countdown HUD** | `UI::renderCountdownHud` (ImGui) | a HUD module (Repeater/StatReadout) |
| **Dialogue** | `DialogueSystem` is **logic-only (no ImGui)**; only its host-side *rendering* is ImGui | a `UISystem` dialogue panel reading `DialogueSystem` state |
| **Standalone host** | `examples/minimal_game/MinimalGame.cpp` + the scaffold (`tools/create_project.py`) call `imgui->newFrame/endFrame` each frame and delegate to `GameMenuRenderer` | drive `UISystem`; ImGui only behind a debug flag |
| **Combat HUD** | `renderCombatHUD` (editor ImGui) | default HUD modules (§10); deleted from `ImGuiRenderer` |

End state: a packaged game does **not initialize ImGui for UI** — only an optional, compile-out
**debug overlay** may use it. ImGui remains the editor app's primary UI.

## 11b. Open items / risks

- Editor Game-view render-target choice (§5a (a) vs (b)) — settle at implementation.
- `UIRenderer` needs **TTF font support** for the BG3 look (R8 bitmap atlas today). This is now
  **required** (not optional) since `UISystem` must fully replace the ImGui menu/screen path; port
  `GameMenuRenderer`'s TTF loading or add a glyph-atlas baker to `UIRenderer`. (Arbitrary RGBA
  **image** textures are DONE — the multi-texture/descriptor-set + draw-run plumbing a TTF glyph
  atlas would reuse.)
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
