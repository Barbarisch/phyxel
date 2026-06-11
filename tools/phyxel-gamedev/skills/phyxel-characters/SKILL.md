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

### Camera style & controls — declare, don't code
The camera block picks one of four engine camera rigs via `"mode"`, and a control scheme via
`"controlScheme"` — per scene in multi-scene games (each scene's `definition.camera`; the
engine re-resolves on every transition, so levels can differ):

| `mode` | View | Pair with |
|--------|------|-----------|
| `first_person` | through the player's eyes | `fps` (maze/shooter) |
| `third_person` | chase cam behind the player | `fps` or `tank` |
| `overhead` | straight-down orthographic map | `tank` |
| `isometric` | classic fixed ~35° ortho 3/4 view | `tank` |

`controlScheme`: `fps` = mouse-look + W/S walk-where-you-look + A/D strafe (default);
`tank` = A/D turn the body, W/S forward/back, RMB orbits.

```json
"camera": { "position": {"x":2,"y":24,"z":-6}, "yaw": 90, "pitch": -25,
            "mode": "isometric", "controlScheme": "tank" }
```

Live iteration: `set_camera` accepts the same `mode` values + `control_scheme` — switch a
running game/editor between styles instantly to find what fits. Defaults when unauthored:
first-person + fps. Full design: engine `docs/CameraControlSystem.md`.

### When JSON isn't enough — override the engine classes (C++ projects only)
A scaffolded game's class subclasses `Phyxel::Core::GameShell`; that subclass is the game's
own code and the intended customization surface. Escalate in order:
1. **Virtual hooks** on your game class: `defaultRigName()` / `defaultSchemeName()` (change
   the unauthored defaults) and `onCameraRigResolved(Graphics::CameraRig& rig)` (tweak knobs:
   `rig.distance`, `rig.fov`, `rig.eyeHeight`, `rig.orthoScale`, pitch clamps).
2. **Custom rig/scheme subclass**: derive from `Graphics::CameraRig` (override `update()` —
   e.g. a wall-aware third-person that shortens `distance` near geometry) or
   `Input::ControlScheme` (override `sample()` for bespoke key maps), then install it with
   `gameplayCamera().setRig(std::make_unique<MyRig>())` / `.setScheme(...)` — live-safe on
   any frame. Engine movement convention: **negative `forward` = move forward**.
Then rebuild (`cmake --build build --config Debug`) and repackage. JSON-only dev projects
(`phyxel new`, no C++ sources) can't do this — their ceiling is game.json + MCP.

## NPCs (`create_game_npc`, or the `"npcs"` array)
Each NPC needs a **unique name**, a **position** above the surface (terrain height + ~2), and
a **behavior**: `idle` (stays), `patrol` (waypoints), `wander` (random). Optional:
- **dialogue** — a tree: `{id, startNodeId, nodes:[{id, speaker, text, ...}]}`.
- **storyCharacter** — `{id, faction, agencyLevel, traits{...}, goals[...]}` for story-aware NPCs.

### Dialogue → gameplay state (actions, conditions, trigger events)
Conversation outcomes are machine-readable — "convince 3 NPCs, then the win unlocks" is
fully declarative:
- **Node actions** (run when the node is shown): `"actions": [{"type":"set_story_variable",
  "name":"greta_secret","value":true}, {"type":"complete_objective","id":"obj_greta"}]`.
  Same vocabulary as trigger `then` entries (also `transition_scene`, `quit_game`).
- **Choice conditions** (hide a choice until earned): `"condition": {"variable":"greta_trust",
  "equals":true}` on a choice — also `not_equals` / `gte` / `lte` / `"exists":true`. A missing
  variable FAILS CLOSED (choice hidden).
- **Trigger event**: every node shown fires `dialogue_node_reached` `{tree, node, speaker}` —
  gate triggers on conversation progress:
  `{"when":{"event":"dialogue_node_reached","node":"give_secret"}, "then":[...]}`.
- Combine with a counting pattern: each informant's final node sets its own variable +
  completes an objective; a trigger on `objective_complete` (or a final dialogue choice
  conditioned on all three variables) transitions to the win scene.

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
