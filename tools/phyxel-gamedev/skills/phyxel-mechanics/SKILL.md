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
`fail_objective`, `get_objectives`. Active ones show in a top-right HUD panel (up to 5; higher
priority shown first).

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

## Menus / UI
`create_menu`, `add_menu_element`, `set_menu_element`, `show_menu` / `hide_menu` /
`toggle_menu`, `open_menu_submenu`. Build title screens, settings, custom HUDs.

## Pause
ESC toggles pause (freezes sim + shows menu); `toggle_pause` / `get_pause_state`.

> A standalone game also has a D&D RPG layer (dice, ability scores, classes, spells,
> conditions, currency, rest, world clock) — see the `resources/rpg/` data + the rpg MCP tools
> if the game needs tabletop rules.
