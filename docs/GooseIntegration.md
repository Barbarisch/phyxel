# Goose AI Integration Architecture

## Vision

Integrate [block/goose](https://github.com/block/goose) into phyxel to create a dual-purpose agentic AI system:

1. **Developer-facing**: AI assistance for building games with the engine (asset creation, level design, scripting, debugging)
2. **Runtime-facing**: AI-driven in-game characters with composable "skills," orchestrated storytelling, and emergent NPC behavior

## How Goose Maps to Phyxel

### Goose Concept → Phyxel Usage

| Goose Concept | What It Does | Phyxel Mapping |
|---|---|---|
| **goose-server** (goosed) | REST API server for agent sessions | Runs as a sidecar process; phyxel's C++ engine communicates via HTTP |
| **Agent** | Core LLM loop: prompt → tool calls → responses | Each in-game AI entity runs as an agent session |
| **Extensions** (MCP servers) | Tool providers (file I/O, code execution, etc.) | **Phyxel MCP Extension** — exposes engine APIs as tools the AI can call |
| **Subagents** | Spawned child agents for subtasks | Individual "skills" within a character (combat, dialog, navigation) |
| **Recipes** | YAML-defined workflows with instructions + extensions | Character archetypes, quest scripts, story beats |
| **Sub-recipes** | Nested recipes for multi-step orchestration | Story arcs composed of individual quest recipes |
| **Sessions** | Persistent conversation state | Per-character memory (what they've seen, said, decided) |
| **Providers** | LLM backends (OpenAI, Anthropic, Ollama, etc.) | Configurable per-deployment; local Ollama for offline play |

## Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                         PHYXEL ENGINE (C++)                         │
│                                                                     │
│  ┌──────────────┐  ┌──────────────┐  ┌────────────────────────────┐ │
│  │ Game Loop    │  │ Entity System│  │ GooseBridge (new)          │ │
│  │ Physics      │  │ Characters   │──│  • HTTP client to goosed   │ │
│  │ Rendering    │  │ Player       │  │  • Session management      │ │
│  │ Audio        │  │ NPCs         │  │  • Event queue (async)     │ │
│  └──────────────┘  └──────────────┘  └─────────────┬──────────────┘ │
└────────────────────────────────────────────────────┼────────────────┘
                                                     │ HTTP/REST
                                                     ▼
┌─────────────────────────────────────────────────────────────────────┐
│                     GOOSE SERVER (goosed)                            │
│                     Rust binary, runs as sidecar                    │
│                                                                     │
│  ┌──────────────┐  ┌──────────────┐  ┌────────────────────────────┐ │
│  │ Agent Core   │  │ Session Mgmt │  │ Extension Manager          │ │
│  │ LLM Provider │  │ Per-NPC state│  │  • Phyxel MCP Extension   │ │
│  │ Subagents    │  │ Conversation │  │  • Developer Tools Ext    │ │
│  └──────────────┘  └──────────────┘  └─────────────┬──────────────┘ │
└────────────────────────────────────────────────────┼────────────────┘
                                                     │ MCP (stdio/HTTP)
                                                     ▼
┌─────────────────────────────────────────────────────────────────────┐
│                   PHYXEL MCP EXTENSION (Python)                     │
│           Exposes engine state as tools the AI can call             │
│                                                                     │
│  Tools:                                                             │
│   • get_nearby_entities(npc_id, radius)                             │
│   • get_world_state(region)                                         │
│   • move_to(npc_id, x, y, z)                                       │
│   • say_dialog(npc_id, text, emotion)                               │
│   • attack(npc_id, target_id, skill_name)                           │
│   • play_animation(npc_id, anim_name)                               │
│   • get_inventory(entity_id)                                        │
│   • get_quest_state(quest_id)                                       │
│   • set_quest_state(quest_id, state)                                │
│   • spawn_entity(template, position)                                │
│   • get_player_info()                                               │
│   • navigate_path(npc_id, waypoints)                                │
│   • emote(npc_id, emote_type)                                       │
│   ... (engine-specific tools grow over time)                        │
└─────────────────────────────────────────────────────────────────────┘
```

## The "Skills as Subagents" Model

Each in-game character is a **Goose agent session**. Complex characters are composed of multiple **subagents**, each representing a "skill":

```
┌─────────────────────────────────────────────┐
│           NPC "Guard Captain Voss"          │
│           (Main Agent Session)              │
│                                             │
│  System Prompt: "You are Guard Captain      │
│  Voss, a veteran warrior who protects the   │
│  eastern gate. You are stern but fair..."   │
│                                             │
│  Orchestrates which skill to activate       │
│  based on game events                       │
│                                             │
│  ┌─────────────┐  ┌─────────────────────┐  │
│  │ Combat Skill│  │ Dialog Skill        │  │
│  │ (Subagent)  │  │ (Subagent)          │  │
│  │             │  │                     │  │
│  │ "Evaluate   │  │ "Respond to player  │  │
│  │  threats,   │  │  questions about    │  │
│  │  choose     │  │  the city, give     │  │
│  │  attacks,   │  │  quest hints,       │  │
│  │  call for   │  │  react to player    │  │
│  │  backup"    │  │  reputation"        │  │
│  │             │  │                     │  │
│  │ Tools:      │  │ Tools:              │  │
│  │  attack()   │  │  say_dialog()       │  │
│  │  move_to()  │  │  emote()            │  │
│  │  emote()    │  │  get_player_info()  │  │
│  └─────────────┘  └─────────────────────┘  │
│                                             │
│  ┌─────────────┐  ┌─────────────────────┐  │
│  │ Patrol Skill│  │ Memory Skill        │  │
│  │ (Subagent)  │  │ (Subagent)          │  │
│  │             │  │                     │  │
│  │ "Follow     │  │ "Track what the     │  │
│  │  patrol     │  │  player has done,   │  │
│  │  routes,    │  │  remember past      │  │
│  │  investigate│  │  conversations,     │  │
│  │  anomalies" │  │  maintain grudges"  │  │
│  └─────────────┘  └─────────────────────┘  │
└─────────────────────────────────────────────┘
```

### How It Works at Runtime

1. **Game event occurs** (player approaches NPC, combat starts, quest trigger fires)
2. **GooseBridge** sends the event as a user message to the NPC's agent session via goose-server REST API
3. **Agent** (with the NPC's personality prompt) decides which skill subagent to invoke
4. **Subagent** executes, calling MCP tools (move_to, say_dialog, attack, etc.)
5. **MCP tool calls** arrive back at the Phyxel MCP Extension
6. **Extension** translates them into engine commands pushed to a **command queue**
7. **Game loop** drains the command queue and executes the actions in the next frame(s)

### Latency Management

LLM calls are inherently async (100ms–2s). The system handles this:

- **Command queue** decouples AI decisions from the game loop (60fps never blocks)
- **Idle behaviors** play while waiting for AI response (patrol loops, idle animations)
- **Priority system**: combat actions get faster/smaller models; ambient dialog can use slower/richer models
- **Local models** (Ollama) for latency-critical decisions; cloud models for rich narrative

## Story Orchestration

A **Story Director** agent sits above individual NPC agents and orchestrates narrative:

```
┌─────────────────────────────────────────────┐
│          STORY DIRECTOR (Recipe)            │
│                                             │
│  "The player has entered Act 2. Tension     │
│   should escalate. The guard captain        │
│   should become suspicious. The merchant    │
│   should offer a secret quest."             │
│                                             │
│  Sub-recipes:                               │
│   • act2_guard_suspicion.yaml               │
│   • act2_merchant_secret.yaml               │
│   • act2_ambient_tension.yaml               │
│                                             │
│  Tools:                                     │
│   • set_quest_state()                       │
│   • trigger_event(event_name)               │
│   • modify_npc_mood(npc_id, mood)           │
│   • spawn_encounter(template, location)     │
└─────────────────────────────────────────────┘
```

Implemented as a Goose **Recipe** with **sub-recipes** — each sub-recipe is a self-contained story beat that the director activates based on game state.

## Developer-Facing AI

The same goose-server also powers developer workflows:

| Use Case | Implementation |
|---|---|
| "Generate a new enemy type with 3 attack patterns" | Recipe that uses the developer MCP extension to write C++/Python code, create animation files, register entity templates |
| "Design a dungeon layout for zone 3" | Recipe that calls world_gen tools + spawns voxel structures |
| "Debug why physics bodies fall through the floor" | Goose session with code-search + terminal extensions, pointed at the phyxel codebase |
| "Create a quest where the player escorts a merchant" | Story recipe that generates quest YAML, NPC recipes, and dialog trees |

## Implementation Phases

### Phase 1: Foundation
- [ ] Build `GooseBridge` C++ class (HTTP client to goose-server)
- [ ] Create `PhyxelMCPExtension` Python MCP server with basic tools
- [ ] Add goosed process lifecycle management (start/stop with engine)
- [ ] Wire up a single NPC to test the full loop

### Phase 2: Character Skills
- [ ] Define Recipe YAML schema for character archetypes
- [ ] Implement subagent-based skill system
- [ ] Build command queue for async AI → engine actions
- [ ] Add idle behavior system for latency masking

### Phase 3: Story Director
- [ ] Create Story Director recipe with sub-recipe orchestration
- [ ] Implement quest state tracking via MCP tools
- [ ] Build event system connecting game triggers to story beats
- [ ] Add NPC mood/disposition system driven by AI

### Phase 4: Developer Tools
- [ ] Create developer-focused MCP extension (code gen, asset creation)
- [ ] Build recipe templates for common game dev tasks
- [ ] Integrate with existing Python scripting console

## File Structure (Planned)

```
include/ai/
    GooseBridge.h           # C++ HTTP client to goose-server
    AICommandQueue.h        # Thread-safe queue for AI→engine commands
    AICharacterComponent.h  # Entity component linking character to agent session

src/ai/
    GooseBridge.cpp
    AICommandQueue.cpp
    AICharacterComponent.cpp

scripts/mcp/
    phyxel_extension.py     # Phyxel MCP server (tools for AI to call)
    story_tools.py          # Story/quest MCP tools
    dev_tools.py            # Developer workflow MCP tools

resources/recipes/
    characters/
        guard.yaml          # Guard archetype recipe
        merchant.yaml       # Merchant archetype recipe
    stories/
        act1_intro.yaml     # Story director recipe
    skills/
        combat.yaml         # Combat subagent recipe
        dialog.yaml         # Dialog subagent recipe
        patrol.yaml         # Patrol subagent recipe
```

## Key Design Decisions

1. **Goose runs as a sidecar process**, not embedded — Rust ↔ C++ FFI is fragile; HTTP is universal and debuggable
2. **MCP is the integration layer** — the standard protocol Goose already uses for tool calling, so phyxel just implements another MCP server
3. **Recipes define characters** — YAML files that game designers can edit without touching C++
4. **Subagents = skills** — natural mapping to Goose's existing subagent infrastructure
5. **Async by design** — AI never blocks the game loop; command queue pattern keeps 60fps
6. **Provider-agnostic** — same system works with local Ollama (fast, free, offline) or cloud models (GPT-4, Claude for richer narrative)
