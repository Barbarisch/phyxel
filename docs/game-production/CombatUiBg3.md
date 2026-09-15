# Combat UI & Controls — BG3-like (workstream, started 2026-09-11)

**Trigger:** the user's first manual test of Ravenmere (2026-09-11): "all characters are clipped into
the ground", HUD text that does not fit its panels, panels overlapping, an "action bar" that is a
Minecraft hotbar of 64-stacks, and no discoverable way to select a target or attack. Direction
(user): *"we should be making our UI look similar and function similar to BG3."*

**Decisions taken (user said "get it done"; confirm or overturn):** items are non-stackable by
default (consumables, ammunition and coins-as-currency stack); WASD stays and gains BG3's
click-to-move / click-to-attack; the action bar copies BG3's layout.

## Design keys (docs/FeatureDesignKeys.md)
- Not a voxel feature: all UI is `UISystem` (retained Vulkan, JSON-authored, no ImGui in shipped
  games — docs/HudSystem.md §2a). Character grounding IS engine physics (G-98).
- API: every widget state is reachable through the shipped `--test` API (`get_screen_state`,
  `ui_click`, `inject_input`) so the harness can drive and assert it.
- Visual test: shipped-build screenshots (`/api/screenshot` on the test port), not the editor.
- Test world: Ravenmere's barrow antechamber (3 enemies, one room) is the fixed combat rig.

## Increments (each: contract · required layer · red test · L4 evidence)
| # | Increment | Contract | Layer | Status |
|---|-----------|----------|-------|--------|
| 1 | Characters stand ON floors | after settle, feet = floor top; lowest drawn voxel within 1 micro | L2 unit + L4 shipped | G-98 FIXED (bulk-mode hole), G-99 FIXED (voxel-box grounding for every rig); L4 pending shipped rebuild |
| 2 | HUD layout system | labels wrap; panels size to children; docking anchors; a load-time lint reports overflow/overlap as defects | L2 (layout unit tests) + L4 screenshot | G-100 FIXED at L4: town lint 0; combat lint 4 → 0 after authoring (health above the bar, turn line under the banner, initiative top-right, spell bar bottom-right with an id) |
| 3 | Targeting feedback | hover outline + ground ring on enemies, cursor state, hit chance on nameplate, Attack button enters targeting, portrait click selects | L4 (ui_click + screenshot) | BUILT (G-101): cursor shapes, target rings, hint line, Attack button; L4 pending |
| 4 | Action bar + inventory model | bar holds actions (attack, off-hand, spells, consumables, dash/jump) + action-economy pips; items non-stackable by default; gold → CurrencySystem | L2 (ItemDefinition tests, migration) + L4 | G-104 item model + wallet BUILT (gold credited in run 52's log); G-106 action bar L4 2026-09-15 (labels fit, clicks route — two repeater defects fixed on the way); consumables/pips/portrait still open |
| 5 | Click-to-move outside combat (G-75) | left click on ground = path + walk; NavGraph route | L3 probe + L4 | |

## Ledger
- 2026-09-11 — G-98 root-caused (see RavenmereGapLedger) after three false leads (rig offset,
  fill path, generation path) each retired by a headless measurement; the shipped instance's
  numbers (y 16.0 on a 17.0 floor) were the decisive evidence. Fix: `finalizeLoadedChunk` ends
  bulk mode. Tests: `CharacterStandingTest.*` (5).
- 2026-09-11 — Increment 2 built: `UIWidget::measureHeight`, `UIPanel::applyAutoSize` (+`maxSize` → scroll), `ui/UILayoutLint.h`, `UISystem::lintLayout`/`visibleScreenNames`, `/api/rpg/ui_lint`, shell lint → defects.jsonl, default_hud.json auto-sized. `UILayoutTest` 3/3.
- 2026-09-11 — Increment 2 L4 on the probe exe: the lint found the four overlaps/clips the user saw; authoring took them to 0. Increment 3 built (G-101).
- 2026-09-11 — Increment 3 L4 found G-102 (attack out of reach did nothing); approach-then-attack built in `PlayerTurnController` with a red/green test.
- 2026-09-15 — Increment 4b L4: the data-driven action bar works in the shipped probe build. Found and fixed behind it: repeater buttons bound `label` instead of `item.label` (blank slots) and `UIRepeater` had no click/hover routing (every bar click swallowed) — `UIActionBarTest.*`; `fitText` buttons stop long spell names overlapping.
- 2026-09-11 — Increment 4a: no 64-stacks (per-item data; consumables 20; unknown = 1/slot), coins → `Currency` wallet with `player.goldText` on the HUD and profile persistence (G-104). G-103: shipped games now have a line pass; the F5 flag that hid the player is never set from gameplay.
