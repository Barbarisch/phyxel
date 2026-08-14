# Presentation & Polish — the menu/screen styling surface (design + tracker wiring)

> Born from the Hearthvale BG3 slice (2026-08-14): the user watched runs all week and the
> first things they called out were presentation — off-center titles, static menus, jank.
> Functional validation is provably blind to this (README §4f); this doc makes presentation
> a TRACKED, OFFERED surface instead of something a session forgets.

## 1. The engine/game split (the governing rule)

**Engine** owns capabilities: widgets, alignment, fonts, themes, animation primitives,
camera rigs/paths, scene types. **Games** own expressions of them: which font, which
palette, which camera orbit, which words — always as `game.json` / screen-JSON **data**.
The generated scaffold is *glue only*, and should keep shrinking into `GameShell`
(that migration is the stated direction in `GameShell.h`). Litmus test: **if two different
games would both want it, it's engine; if it expresses THIS game, it's data; if it's
neither, it's scaffold glue and a candidate to absorb into the engine.**
The week's parity work followed this rule: combat/progression/inventory went in as engine
subsystems + authorable data; the scaffold got ~40 lines of wiring each.

## 2. Shipped now (2026-08-14)

- **Label alignment** — `"align": "left" | "center" | "right"` on labels; center/right are
  relative to `position.x` (center = centered ON it), matching what every shipped screen
  had already authored. Default left preserves existing layouts. Root cause of the
  "never centered" report: `UILabel::render` always drew from position.x leftward and
  overwrote the authored size (`UIWidget.cpp:203`). All shipped screens' title/message
  labels now author `align: center` (settings row labels deliberately stay left).
- Per-menu **TTF fonts** (`"fonts"` array — file + size per id) — existed, under-used.
- Data-driven screens/menus/HUD on custom-Vulkan UISystem; `{{token}}` interpolation;
  Repeater lists; solid/image backgrounds; the trigger action vocabulary on buttons.

## 3. Animated menu backgrounds — design (next build item)

Two tiers, both authorable:

**Tier 1 — `menuWorld` (the BG3 move, primary).** A menu scene may name a world + camera:
```jsonc
{ "id": "main_menu", "sceneType": "menu",
  "menuWorld": {
    "worldDatabase": "worlds/title.db",          // or inline "world" gen block
    "cameraPath": { "waypoints": [ {"x":..,"y":..,"z":..,"yaw":..,"pitch":..,"dwell":..} ],
                     "loop": true, "duration": 30 } },
  "menuLayout": { ... } }
```
The menu UI renders over a LIVE 3D scene slowly orbiting via the existing
`Graphics::CameraPath` (splines, looping — already in the engine, currently unused by
menus). Engine work: `SceneManager::executeLoad` currently skips ALL world setup for
menu scenes — allow the world path when `menuWorld` is present (load chunks, no player,
no physics tick needed beyond render); drive `CameraPath::update` while the menu scene is
active. Scaffold work: none beyond passing through (the loader owns it) — this is
deliberately an ENGINE feature so every game gets it by authoring two JSON keys.

**Tier 2 — UI-layer motion.** Port the per-element animations the old ImGui menu renderer
had (`fade_in` / `slide_in_left/right` + delays — schema already exists, HudSystem.md §3)
onto UISystem widgets, plus an animated background primitive (scrolling/pulsing gradient)
for menus that want motion without a world.

## 4. Surfacing in interactive game-dev sessions (so this never goes unoffered)

The game-production tracker is the mechanism (README §4b/§4d — milestones + recipes):

- **New CORE milestone `presentation`** (required depth L2, feel-gated): title screen
  styled (font chosen, labels aligned, background not the default solid), screens
  reviewed at target resolution. Sits beside `main_menu`/`hud` — a game can't read
  "done" with the default JetBrains-Mono-on-black title.
- **New recipe `menus-and-style`** (recipe library, §4d): the step-by-step "make it
  pop" playbook a session follows when the user says "improve the menus" — enumerate
  CURRENT options (fonts array, align, themes when they land, menuWorld when it lands,
  per-element animations when they land) with copy-paste JSON, and explicitly list
  what's NOT available yet so the session says "engine gap — filing /feedback" instead
  of hand-rolling. The `gamedev-next` skill already drives from recipes; this plugs
  presentation into that loop.
- **The offering behavior**: when a session touches any screen/menu/HUD JSON, it should
  present the option menu (fonts? alignment? theme? animated background? custom
  layout?) rather than silently accepting defaults — the recipe encodes that prompt.

Tracker data changes live in `docs/game-production/genre-templates/` + `recipes/` and
`tools/phyxel-gamedev/` — see the TODO list at the end.

## 5. TODO (tracked)

- [x] Tier 1 `menuWorld` engine support + Hearthvale sample, pixel-verified (2026-08-14)
- [ ] Tier 2 per-element animation port to UISystem
- [ ] Named JSON themes + BG3 theme (`hud.theme` per game) — HudSystem.md §4 item
- [x] `presentation` milestone added to genre templates; `menus-and-style` recipe written (2026-08-14)
- [x] Standalone screenshot endpoint — shipped + already caught a real bug (2026-08-14)

## 6. Text containment (2026-08-14)

Root cause of text escaping its boxes: no clipping existed anywhere, label wrapWidth was
never authored, and authored label size was dead data. Shipped: (1) a CPU clip-rect stack
in UIRenderer::pushQuad - the single choke point every rect/image/glyph flows through -
clamping partial quads with proportional UVs, so panels contain content WITHOUT breaking
the one-draw-call batch a GPU scissor would split; (2) UIPanel clips its children by
default (opt-out "clip": false; zero-size panels never clip; nested clips intersect);
(3) labels with an authored width auto-wrap to it. Smoke-verified: menu renders + clicks,
dialogue opens. Pixels not machine-verified (screenshot endpoint still TODO).
Scrolling: NOT built - the scrollable-container widget (quest logs, long dialogue, AI
chat) remains the top open widget item (HudSystem.md sec 4); clipping makes overflow
invisible, scrolling makes it reachable.

## 7. Screenshot endpoint + pixel-verified centering (2026-08-14)

GET /api/screenshot now works on STANDALONE games (GameApiService capture_screenshot ->
RenderCoordinator::captureScreenshot -> screenshots/<ts>.png; the shared EngineAPIServer
route existed all along). First use immediately caught a real bug: the align fix had
patched MenuDefinition::buildWidget, but menu scenes AND the intro/victory/credits/loading
overlays build labels through a SECOND path (buildMenuElement) that never parsed align -
the "centered" title was still left-flushed at 640. Both paths now share align/auto-wrap
semantics, and single-line labels center on measured text width (a box-aligned wrap would
have re-broken short titles). Evidence: docs/evidence/hearthvale/menu_before_center.png vs
menu_after_center.png - title/subtitle/buttons all on the 640 centerline, pixel-verified.
Presentation claims can now graduate from smoke+eyes to measured.

## 8. menuWorld SHIPPED (2026-08-14) - the living title screen

Tier 1 is real: a menu scene authors worldDatabase + definition.world (+ structures) and a
definition.cameraPath (waypoints {position,yaw,pitch,dwell}, loop) - the SceneManager loads
the world behind the menu (no player/NPCs/physics; playerDefaults never merged for menus)
and the shell drives the Catmull-Rom orbit every frame the menu is up, releasing the camera
to gameplay on transition. PIXEL-VERIFIED on Hearthvale: menuworld_frameA/B.png, 3.5s apart,
62.9 percent of pixels changed while title+buttons held still; Begin lands in town after.
Authoring cost to a game: JSON only. Known cosmetics: the menu background scrim alpha did
not visibly composite (UI draws direct on the world - readable, but tune scrim blending);
glow material reads as a flat pale slab from above. Build lore reconfirmed: a timed-out
background build KEEPS RUNNING and holds file locks - stop it (TaskStop) and kill orphaned
cl.exe before regenerating, or the regen silently leaves stale files.
