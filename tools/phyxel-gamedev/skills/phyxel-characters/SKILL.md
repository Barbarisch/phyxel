---
name: phyxel-characters
description: Use when adding or editing the player, NPCs, dialogue, or story/quests in a Phyxel game — placing the player and camera, creating NPCs with behaviors, authoring dialogue trees and story arcs/beats. Invoke for "add a character / NPC / villager / enemy / dialogue / quest line" tasks.
---

# Characters, NPCs, dialogue & story

## Player & camera (the `"player"` / `"camera"` blocks)
- **Player type: use `"animated"`** — the .anim-based voxel character (Idle/Walk/Run/Jump/
  etc., grounds against terrain). NOTE: the older `"physics"` and `"spider"` types in some
  docs are **deprecated** (archived with Bullet) — don't use them.
- Position the player just above the surface (e.g. terrain top + 1). Camera: 30–50 units from
  the action, pitch −25° to −35° for an overview. `set_camera` adjusts it live.

## NPCs (`create_game_npc`, or the `"npcs"` array)
Each NPC needs a **unique name**, a **position** above the surface (terrain height + ~2), and
a **behavior**: `idle` (stays), `patrol` (waypoints), `wander` (random). Optional:
- **dialogue** — a tree: `{id, startNodeId, nodes:[{id, speaker, text, ...}]}`.
- **storyCharacter** — `{id, faction, agencyLevel, traits{...}, goals[...]}` for story-aware NPCs.

Inspect/iterate: `list_npcs`, `spawn_npc`, `set_npc_behavior`, `set_npc_dialogue`,
`set_npc_appearance`, `remove_npc`. Dialogue runtime: `start_dialogue`, `advance_dialogue`,
`select_dialogue_choice`.

## Story (the `"story"` block / story_* tools)
Arcs contain **beats** (key moments): `Hard` (must happen, in order under strict modes), `Soft`
(should), `Optional` (enrichment). **Constraint modes**: `Strict` (exact order), `Guided`
(general order), `Open` (any order). Beats can list `requiredCharacters`. Tools: `story_add_arc`,
`story_add_character`, `story_trigger_event`, `story_get_state`.

## Conversational AI (optional)
NPCs can use the AIConversationService (Claude/OpenAI/Ollama) — `configure_ai`,
`start_ai_conversation`, `send_ai_message`. Personality/voice derive from the story character.

Author the whole cast at once inside `load_game_definition`, then refine with the per-NPC tools.
