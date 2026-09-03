# Recipe: menu & screen STYLE — make it pop

**Satisfies milestone:** `presentation` (core). **Genre:** any.
**Sibling:** `menus-and-screens.md` covers which screens EXIST; this covers how they LOOK.
Born from the Hearthvale slice: the user watched runs for days and the first feedback was
presentation — off-center titles, static screens, text escaping boxes. None of it blocked
an L4 milestone; all of it read as jank.

## THE OFFERING RULE (why this recipe exists)

**Whenever a session touches any screen/menu/HUD JSON, present the style option menu instead
of silently accepting defaults.** Enumerate what's available (below), show the user what
their screen COULD look like, and for anything not yet supported say plainly: *"engine gap —
filing /feedback"* — never hand-roll around a missing engine capability. Defaults are a
fallback, not a decision.

## The option menu (current engine capabilities, all JSON)

1. **Fonts** — per-menu `"fonts"` array: `[{ "id": "title", "file": "resources/fonts/<any>.ttf",
   "size": 52 }]`; widgets reference `"font": "title"`. Any TTF you drop in resources/fonts.
2. **Alignment** — labels: `"align": "left" | "center" | "right"` (center/right are relative
   to `position.x`; center = centered ON it). Titles/messages want center; row labels left.
   ⚠ TWO label build paths exist (`buildWidget` + `buildMenuElement`) — both support align
   now; if adding label features, patch BOTH (screenshot-verify, it caught this once).
3. **Text containment** — labels with an authored `"size"` auto-wrap to that width; panels
   clip their children by default (`"clip": false` opts out). No scrolling yet (gap, below).
4. **Backgrounds** —
   - `"background_type": "solid"` + `"background_color": [r,g,b,a]`
   - `"background_type": "image"` + a full-screen art image
   - **menuWorld (the BG3 living title screen)**: give the menu scene a `worldDatabase`,
     a `definition.world` (+ `structures`), and a `definition.cameraPath`:
     `{ "loop": true, "waypoints": [{ "position": {x,y,z}, "yaw": -180, "pitch": -22,
     "dwell": 0.5 }, ...] }` — the menu renders over a live, slowly-orbiting 3D scene.
     Pixel-proven: 62.9% of pixels moving under a rock-still UI.
5. **Screen JSONs are per-game overridable** — copy any `resources/ui/*_screen.json` into
   the project's `resources/ui/` (project files win over engine defaults at build-asset copy).
6. **HUD layout** — top-level `"hud"` array in game.json replaces the default HUD panels.

## Verify (the presentation milestone's L2 bar)

`GET /api/screenshot` on the running standalone (`--test`) → Read the PNG. Check: titles
centered, text inside its boxes, background not the default solid, readable at 1280×720.
For animated backgrounds: two captures ≥3 s apart must differ substantially (pixel diff).
Archive before/after captures as evidence.

## Themes (2026-08-19) — offer these EVERY session

`"theme"` on any menuLayout / screen JSON: either a preset name or an inline object.
Presets ship in `resources/ui/themes/` — **slate** (default cool grey), **ember** (warm
firelit tavern), **parchment** (light aged paper / codex). Inline objects override any
subset of keys (`buttonBg`, `titleColor`, `padding`, … — full list in UITheme /
MenuDefinition::applyThemeJson); colors are `[r,g,b(,a)]` floats 0-1. A game can also
copy a preset into its own `resources/ui/themes/` and edit it. Offer the three presets
by name + "or a custom palette" whenever a game's menus come up.

## Per-element animations (2026-08-19)

Any menu element takes `"animation"`: `fade_in`, `slide_in_left`, `slide_in_right`,
`slide_in_up`, with `"animation_delay"` / `"animation_duration"` (s). Stagger delays
across buttons for the cascade look (Hearthvale: title fade 0.8s, buttons slide-left at
0.15/0.30/0.45). Replays every time the screen is (re)shown — submenus included. Same
schema as the old ImGui renderer, so `samples/game_definitions/menu_demo.json` authoring
carries over. Verified: early-vs-settled captures differ ~200k px, reopen replays.

## Known gaps — say "engine gap", file /feedback, do NOT hand-roll

- **Scrolling: BUILT 2026-08-19** (`"scrollable": true` panels + nested `"panel"` menu
  element). Text-input widget exists; AI-dialogue standalone wiring still pending.
- **Menu scrim over menuWorld: NOT a gap — measured working 2026-08-19.** A/B pixel test
  (alpha 0.45 → world grass (123,134,114); 0.95 → (60,56,75)) matches the expected
  `(1-a)·world + a·scrim` blend. The old "not compositing" note was a misdiagnosis: 0.45
  near-black over a bright pastel world is just subtle. **Authoring guidance: 0.55-0.70
  for a legible menu over a visible world** (Hearthvale ships 0.62); 0.9+ ≈ opaque.
- **menuWorld art direction:** a multi-voxel `glow` fill blooms into a screen-filling flat
  blob from the emissive pass — use SINGLE glow voxels as lanterns/accents on non-emissive
  structures (the underlying glow-bloom scaling is still an engine issue, tracked).
- **Per-element colors: BUILT 2026-08-19.** Labels: `"color"` + `"scale"`; buttons:
  `"color"` / `"bg"` / `"bgHover"`. Use for accent buttons (a danger-red Quit, a gold
  call-to-action) and secondary text. Custom typefaces remain the real gap — one bitmap
  font today (engine gap; file /feedback if a game needs it).
