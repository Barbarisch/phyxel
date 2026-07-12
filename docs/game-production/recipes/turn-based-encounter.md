# Recipe: turn-based encounter (D&D-style)

**Satisfies milestone:** `combat` (combat_model: `turn_based`), content toward `campaign_structure`.
**Genre:** rpg. Phyxel ships the ruleset (Initiative/ActionEconomy/AttackResolver/ConditionSystem/
DiceSystem) — this recipe **stages an encounter** with it. For real-time instead, see
`real-time-combat` (combat_model: `real_time`).

## 1. Design-first (GAMEPLAN.md)
Specify: combatants (player + party vs which enemies), the encounter's win/lose (all enemies down /
party down / objective), the arena, and any conditions/hazards in play. Add matrix rows for the
condition interactions that matter here (e.g. `condition(prone) x melee-attack -> advantage`).

## 2. Stage it
- Build the arena (structure gen / templates) with room to maneuver.
- Place combatants; give enemies stat blocks (EncounterBuilder / the rpg data in `resources/rpg/`).
- `start_combat` -> `set_initiative` (or let the system roll) -> the initiative order drives turns.

## 3. Drive a turn
- `next_combat_turn`; on the active actor, spend the action economy: `attack` (AttackResolver rolls
  to-hit vs AC, then damage), `cast_spell`, move, or apply a condition (`apply_damage`,
  ConditionSystem). Confirm dice + modifiers resolve against the right ability (`roll_dice`,
  `check_dc`).
- Loop until a win/lose state; fire the encounter's outcome (rewards / story variable / transition).

## 4. Validate (red-before-green)
- L2: combatants have valid stat blocks; the win/lose condition references real state.
- L3: run the encounter start->resolution in-engine — initiative orders correctly, an attack hits &
  deals rules-correct damage, a condition applies and expires, the outcome fires. Capture
  `get_combat_state` evidence.
- L4: a human/adversarial playthrough confirms it's winnable *and not trivial* (ties to
  `difficulty_balance` — CR vs party power).
- Record `combat -> validated:L3/L4`; bump `content.encounters.current`.

## 5. Feel pass (Axis C)
Hit/miss SFX, damage-number pop, a screenshake on crits, clear turn/telegraph indicators. Set
`combat.feel = passed` after.
