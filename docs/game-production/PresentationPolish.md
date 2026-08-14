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

- [ ] Tier 1 `menuWorld` engine support (SceneManager + CameraPath drive) + sample
- [ ] Tier 2 per-element animation port to UISystem
- [ ] Named JSON themes + BG3 theme (`hud.theme` per game) — HudSystem.md §4 item
- [ ] `presentation` milestone added to genre templates; `menus-and-style` recipe written
- [ ] Standalone screenshot endpoint (GameApiService) so presentation gets pixel-verified
