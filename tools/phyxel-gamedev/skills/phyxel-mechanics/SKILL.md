---
name: phyxel-mechanics
description: Use when adding gameplay systems to a Phyxel game — player health/respawn, objectives/quests, background music, day/night cycle, combat/equipment, crafting, menus/HUD, and pause. Invoke for "add health / a quest objective / music / a day-night cycle / combat / a menu" tasks.
---

# Gameplay mechanics

Layer these on after the world, characters, and story exist. All are MCP-driven on the live
engine; persist with `save_world` / `save_player`.

## Health & respawn
`set_spawn_point` (where the player respawns), `damage_player` / `heal_player` / `kill_player`
/ `revive_player`, `get_player_health` / `get_respawn_state`. At 0 HP a death overlay + respawn
timer trigger.

## Objectives / quests
`add_objective` (title, description, priority, category), `complete_objective` /
`fail_objective`, `get_objectives`. They appear in the default HUD's **Objectives panel
(top-left)** with `[x]`/`[ ]` markers, priority-sorted; the panel auto-hides when there are none.

## Music
`control_music` with `action`: `add_track` (queue files), `play`, plus Sequential/Shuffle modes
and volume (0.0–1.0). Persists via `save_player`/`load_player`. State: `get_music_state`.

## Day / night
`set_day_night` (time of day, day length, time speed). Dawn 6:00, Day 8:00, Dusk 18:00, Night
20:00; ambient + sun colors animate. `get_day_night`.

## Combat & equipment
`attack` (sphere+cone hit + knockback), `damage_entity` / `heal_entity`, `equip_item` /
`unequip_item` (6 slots), `get_equipment`. Health on any entity: `set_entity_health`.

## Crafting & inventory
`add_recipe` (ingredients → output), `craft_item`; `give_item` / `get_inventory` /
`set_inventory_slot` / `select_hotbar_slot`.

## HUD (data-driven UISystem — no ImGui)
The engine ships a **default HUD out of the box** (`resources/ui/default_hud.json`): health bar
(bottom-left), hotbar with item icons (bottom-center), objectives (top-left), the dialogue box,
and the combat HUD — all on the custom-Vulkan `UISystem`. Modules show/hide by live state
(objectives appear only when objectives exist; the combat HUD only during turn-based combat).
**Customize per game** by adding a top-level `"hud"` array to `game.json` (it OVERRIDES the
default): each entry is an anchored panel of widgets — `progressbar` / `label` (supports
`wrapWidth`) / `repeater` (list-driven, e.g. hotbar/objectives) / `image` / `button` — with
`bind` to live state (`player.health`, `combat.turn_order`, `dialogue.text`, …) and `visibleWhen`
gates. Full schema + widgets: the engine's `docs/HudSystem.md`.

## Menus
A **menu scene** (`sceneType:"menu"`) carries a `menuLayout` (1280×720 canvas:
panels/labels/buttons/images; button `action` = `transition_scene` / `quit_game` /
`open_submenu` / `close_submenu`; `{{playtime}}` and `{{story.<var>}}` token interpolation in
text). It renders via the UISystem (no ImGui). The live MCP menu tools (`create_menu`,
`add_menu_element`, `set_menu_element`, `show_menu`/`hide_menu`/`toggle_menu`,
`open_menu_submenu`) build UISystem menu screens too.

## Pause
ESC toggles pause (freezes sim + shows menu); `toggle_pause` / `get_pause_state`.

> A standalone game also has a D&D RPG layer (dice, ability scores, classes, spells,
> conditions, currency, rest, world clock) — see the `resources/rpg/` data + the rpg MCP tools
> if the game needs tabletop rules.
