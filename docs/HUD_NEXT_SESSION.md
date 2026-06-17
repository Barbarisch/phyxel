# HUD System — Next-Session Kickoff

> **Point a fresh Claude session at this file to start the Game-HUD system work.**
> It is a self-contained brief: the goal, why it's its own session, the current UI layers
> (grounded in real files), the design forks to decide, a suggested first-steps plan, and the
> combat HUD as the first concrete customer. Read `docs/AgentContext.md` too (general working
> context), then this.
>
> Status when written (2026-06-17): turn-based combat S1–S7 + spellcasting + AoE are DONE and on
> `main`. The combat HUD built during that work is a **stopgap in the wrong layer** — fixing that
> is the motivation here. **Do not start coding before the design forks below are decided with the
> user** (the user wants a thorough design discussion before building large features).

---

## 1. The goal (user's vision)

The engine should provide a **rich HUD feature set for game developers**:

- A **default HUD** the engine ships out of the box (health, hotbar, objectives, dialogue,
  combat widgets, …).
- **Rich customization** so developers create interesting, unique HUDs — "defaults are fine,
  customization makes it pop."
- A clear story for **how a HUD appears in the editor (preview) vs. in the actual shipped game**.

The HUD is a real engine subsystem, not a one-off. Design it as something games author against.

## 2. Why this needs its own session — the stopgap problem

During the turn-based combat rollout we added a combat HUD (`COMBAT` banner, initiative panel,
action bar, hit-chance readout). It was the right call for **verifying** combat in-engine, but it
is **architecturally misplaced**:

- It is **raw ImGui drawn in the EDITOR's render path** (`engine/src/ui/ImGuiRenderer.cpp`,
  function `renderCombatHUD`, called from `editor/src/Application.cpp`).
- So it **overlaps the editor dev panels** (World Outliner / Properties / Viewport) — the overlap
  the user noticed — and it **conflates editor chrome with a game HUD**.
- It would **not ship correctly**: a packaged game needs its own HUD in the game-facing UI layer,
  not the editor's panel renderer.

Treat the combat HUD as **throwaway scaffolding**. It proved the data + interactions; it is not
the foundation. Polishing it ("S8 HUD polish") is explicitly **blocked** until the system below
exists. See `docs/TurnBasedCombat.md` → S8.

## 3. Current UI layers (grounded — verify before relying)

There are **two ImGui-based UI layers** today; the editor-vs-game split partly exists but is not
cleanly enforced (the combat HUD leaked across it).

| Layer | Files | What it is | Ships in a game? |
|-------|-------|-----------|------------------|
| **Editor dev UI** | `engine/src/ui/ImGuiRenderer.cpp` (+ `.h`) | The editor's panels: World Outliner, Properties, Viewport, lighting/spell-caster tools, **and the misplaced `renderCombatHUD`** | No — editor only |
| **Game-facing UI** | `engine/src/ui/GameMenus.cpp` / `GameMenuRenderer.{h,cpp}` (menus, `{{token}}` interpolation), `GameScreen.h` (`ScreenState` Intro/Victory/Credits + `renderIntro/Victory/CreditsScreen`), `UI::renderCountdownHud` (in `GameMenus.cpp`), `DialogueSystem.{h,cpp}` | Menus, screens, dialogue, the timer countdown HUD — **also ImGui**, used by the editor's play AND the packaged standalone | **Yes** |
| **Game shell** | `engine/include/core/GameShell.h` + `engine/src/core/GameShell.cpp` | Engine-side base class consolidating standalone shell logic (camera/control done; menu-wiring/triggers migrating). The packaged game subclasses it. | Yes |

Key takeaways:
- **ImGui is already the shared backend** for both editor and game-facing UI. So the "rendering
  backend" question is *not* "ImGui or not" — it's whether ImGui (immediate-mode) is the right
  long-term basis for **authorable, themeable, shippable** HUDs, or whether the game HUD wants a
  **retained, data-driven** layer on top of (or instead of) it.
- The **game-facing layer already exists** (`GameMenus`/`GameScreen`/`DialogueSystem`) — the HUD
  system should grow *there* and unify these, not live in the editor's `ImGuiRenderer`.
- A game is packaged separately (`tools/package_game.py`, `tools/create_project.py`,
  `examples/minimal_game/`) and runs the `GameShell` standalone; the HUD must render there with no
  editor present.

## 4. Design forks to decide with the user (do this first)

1. **Editor-vs-game rendering split (the core one).** A game HUD must render **over the game
   scene, not as editor panels**. Decide the model: a **play/game view** (editor chrome hidden,
   the game HUD overlays the viewport — like Unity's Game view / play-in-editor) so devs preview
   exactly what ships. Today there's no clean boundary; we need one. (The game-facing layer already
   renders in the standalone; the gap is a faithful in-editor preview.)

2. **Authoring model.** The "default + rich customization" goal points to **data-driven,
   composable HUD definitions** — declarative widgets + layout + style/theme + data bindings,
   authored in `game.json`/assets — with a **code/scripting escape hatch** for bespoke widgets.
   Alternative: everything in code via `GameCallbacks`/`GameShell` (won't give easy customization).
   Decide where on that spectrum, and the schema shape.

3. **Rendering backend.** Keep **ImGui** for game HUDs (pragmatic — already used for menus, links
   into the standalone) vs. invest in a **retained game-UI renderer** (more work, cleaner theming
   + customization + perf control). This is the biggest cost/architecture fork.

4. **Data binding.** How widgets bind to live game state uniformly + extensibly: health,
   initiative/turn order, action budget, inventory/hotbar, objectives, story variables, custom.

5. **Default modules + theming.** The shipped default HUD as **composable modules** a dev can
   reposition / restyle / replace, plus themes (colors, fonts, layout, animation) that "make it
   pop."

## 5. Suggested first-steps plan for the session

1. **Map the current UI layers precisely** (extend §3): read `ImGuiRenderer.cpp`, `GameMenus.cpp`,
   `GameMenuRenderer`, `GameScreen.h`, `DialogueSystem`, `GameShell`, and how
   `editor/src/Application.cpp` vs the scaffold/`minimal_game` decide what to render. Confirm
   exactly what's editor-only vs game-facing today.
2. **Resolve the five forks** with the user (§4) — design discussion, not code.
3. **Write the design doc** (`docs/HudSystem.md`) — the architecture + schema + module list.
4. **Build a thin vertical slice**: one data-driven HUD widget (e.g. a health bar) bound to live
   state, rendering in BOTH the editor preview and the standalone, authored from `game.json`.
   Verify by running (build → launch → screenshot in editor preview AND a packaged/standalone run).
5. **Re-home the combat HUD** as the first set of default modules (§6) once the slice works.

## 6. First real customer — the combat HUD

When the system exists, re-express the combat HUD as **default HUD modules** (this is the forcing
function that validates the design):

- Turn-order **portrait bar** (currently the initiative panel).
- **Action bar** (Action / Bonus / Movement remaining / End Turn button).
- **Hit-chance** tooltip/readout for the selected/hovered target.
- The planned-but-unbuilt S8 pieces: d20 roll + crit/miss callouts, **floating damage numbers**,
  on-ground **movement-range ring + path spline**, target highlight.

The data is all available from `Core::CombatDirector` / `Core::PlayerTurnController` /
`Core::InitiativeTracker` (see `docs/TurnBasedCombat.md`). The current `renderCombatHUD` shows the
intended *content*; the system defines the right *home, authoring, and rendering* for it. Once
re-homed, **delete `renderCombatHUD` from the editor `ImGuiRenderer`.**

## 7. Constraints & user working preferences (carry into the design)

- **Performance is a first-class constraint** (goal: rich worlds + 100s of characters on screen).
  HUD rendering runs every frame — keep it cheap; measure. Debug-build numbers aren't shippable.
- **Single source of truth** for state; HUD widgets *read* game state, don't duplicate it.
- **Incremental; avoid big rewrites and over-abstraction.** Grow the existing game-facing UI
  layer; don't reinvent. If investigation shows a refactor isn't warranted, say so.
- **Verify by running** (build → launch → screenshot), not just "compiles."
- **Editor UI conventions:** action buttons (Reset/Delete/…) always visible regardless of state;
  per-object properties on the object's panel; global settings on their own panel. (Relevant if we
  add a HUD-authoring/preview panel to the editor.)
- **Thorough design discussion before building** — resolve §4 with the user first.

## 8. Open questions for the user (raise at session start)

- Play/game-view preview in the editor, or a dedicated HUD-preview panel, or both?
- Data-driven `game.json` HUD definitions as the primary authoring path? How much code/scripting
  escape hatch?
- Stay on ImGui for game HUDs, or build a retained game-UI renderer?
- How much theming/skinning is in scope for v1 vs later?
- Is there reference art / a target look (BG3-style, Minecraft-style, custom)?

## 9. Pointers

- Combat data + the stopgap: `docs/TurnBasedCombat.md` (esp. S8).
- General working context + the turn-based workstream: `docs/AgentContext.md`.
- Per-machine memory: `hud-system-architecture-track` (project note: combat HUD is a stopgap;
  don't add more game-HUD to the editor until designed).
- Camera/control precedent for a shared engine-side abstraction: `docs/CameraControlSystem.md`
  (the `CameraRig`/`ControlScheme`/`GameplayCameraController` pattern is a good model for how a
  HUD system might be authored once and used by both editor and standalone).
- Packaging / standalone: `tools/package_game.py`, `tools/create_project.py`,
  `examples/minimal_game/`.
