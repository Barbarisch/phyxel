# Scene System (Multi-Level Games)

The Scene System lets a game define multiple **scenes** — independent world states or levels — that share a persistent story engine, player profile, and inventory. Each scene has its own chunk database, entities, NPCs, lighting, and camera defaults.

## Architecture

```
SceneManifest          ← parsed from game.json "scenes" array
  ├─ SceneDefinition   ← one per level (id, name, worldDatabase, definition JSON)
  ├─ startScene        ← ID loaded on startup
  ├─ playerDefaults    ← merged into scenes without "player"
  └─ globalStory       ← story arcs that persist across transitions
                         
SceneManager           ← lives in EngineRuntime
  ├─ state machine     ← Idle → Unloading → Loading → Ready
  ├─ SceneCallbacks    ← host wiring (Application or standalone game)
  ├─ reentryStates_    ← per-scene player position saved on exit
  └─ GameSubsystems*   ← ChunkManager, Camera, EventLog, etc.
```

### State Machine

```
   loadStartScene() / transitionTo()
              │
              v
   ┌──────────────────┐
   │    Unloading      │  endDialogue → onExitScript → clearNPCs →
   │  (active scene)   │  clearEntities → clearStoryChars → resetLighting →
   │                   │  clearMusic → clearObjectives → clearLocations →
   └───────┬───────────┘  savePlayerPos
           │
           v
   ┌──────────────────┐
   │     Loading       │  saveDirtyChunks → clearDynamics → cleanup →
   │  (target scene)   │  switchDB → loadChunks → GameDefinitionLoader::load →
   │                   │  restoreReentryPos → rebuildNavGrid → onEnterScript →
   └───────┬───────────┘  markVisited → emit("scene_loaded")
           │
           v
   ┌──────────────────┐
   │      Ready        │  activeSceneId_ = target, onSceneReady callback
   └──────────────────┘
```

### What Persists Across Transitions

| Persists | Cleared |
|----------|---------|
| StoryEngine arcs & world state | Chunks (reloaded from DB) |
| Player health, inventory, equipment | Entities & NPCs |
| Achievement progress | Lighting / day-night |
| Player profile & XP | Music playlist |
| Global story characters | Objectives |
| Camera position (saved per-scene) | Locations |

> Note: this table describes what `SceneManager` is *designed* to do via `SceneCallbacks`. As of
> this writing, only entity/NPC clearing, dialogue-ending, and location clearing are actually wired
> by the editor and by `create_project.py`'s generated scaffold — see the "Known gap" callout under
> Editor Integration below for which callbacks are still no-ops in both hosts.

## Game Definition Format

A multi-scene game.json has a `"scenes"` array instead of a top-level `"world"` key:

```json
{
  "startScene": "village",
  "playerDefaults": {
    "type": "animated",
    "animFile": "character_complete.anim",
    "position": [16, 20, 16]
  },
  "globalStory": {
    "arcs": [
      {
        "id": "main_quest",
        "name": "The Main Quest",
        "stages": ["start", "middle", "end"]
      }
    ]
  },
  "scenes": [
    {
      "id": "village",
      "name": "Peaceful Village",
      "worldDatabase": "village.db",
      "transitionStyle": "fade",
      "definition": {
        "world": { "type": "Perlin", "seed": 42 },
        "camera": { "position": [50, 30, 50] },
        "npcs": [ ... ],
        "story": { "arcs": [ ... ] }
      }
    },
    {
      "id": "dungeon",
      "name": "Dark Dungeon",
      "worldDatabase": "dungeon.db",
      "transitionStyle": "loading_screen",
      "onEnterScript": "scripts/enter_dungeon.py",
      "definition": {
        "world": { "type": "Caves", "seed": 99 },
        "npcs": [ ... ]
      }
    }
  ]
}
```

### SceneDefinition Fields

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `id` | string | yes | Unique scene identifier |
| `name` | string | no | Display name (defaults to `id`) |
| `worldDatabase` | string | no | SQLite DB filename, resolved relative to the project `worlds/` dir (defaults to `<id>.db`). A leading `worlds/` prefix is tolerated and stripped — `"worlds/foo.db"` and `"foo.db"` both resolve to `worlds/foo.db`. |
| `description` | string | no | Optional description text |
| `transitionStyle` | string | no | `"cut"`, `"fade"`, or `"loading_screen"` (default) |
| `onEnterScript` | string | no | Python script run when entering (currently inert — see Editor Integration "Known gap") |
| `onExitScript` | string | no | Python script run when leaving (currently inert — see Editor Integration "Known gap") |
| `definition` | object | no | Full game definition (world, camera, npcs, story, etc.) |
| `sceneType` | string | no | `"World"` (default), `"Menu"` (no world DB, uses `menuLayout`), or `"Cutscene"` (future) — not in the original design, added since |
| `menuLayout` | object | no | Menu UI tree (root `UIPanel`), only used when `sceneType == "Menu"` |

### Manifest Fields

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `scenes` | array | yes | Array of SceneDefinition objects |
| `startScene` | string | no | ID of initial scene (defaults to first) |
| `playerDefaults` | object | no | Merged into scenes missing `"player"` |
| `globalStory` | object | no | Story config that persists across transitions |

## Re-entry System

When a player leaves a scene, the SceneManager saves their camera position. If they return later, the position is restored automatically.

```cpp
// Saved during unload
reentryStates_[activeSceneId_] = { visited: true, lastPlayerX, Y, Z };

// Restored during load (if scene was previously visited)
camera->setPosition(glm::vec3(rs.lastPlayerX, rs.lastPlayerY, rs.lastPlayerZ));
```

## API & MCP Tools

### HTTP Endpoints (port 8090)

| Route | Method | Description |
|-------|--------|-------------|
| `/api/scenes` | GET | List all scenes |
| `/api/scene/active` | GET | Active scene details |
| `/api/scene/:id` | GET | Get a specific scene definition by ID |
| `/api/scene/transition` | POST | Transition to scene by ID |
| `/api/scene/add` | POST | Add scene at runtime |
| `/api/scene/update` | POST | Update fields of an existing scene definition |
| `/api/scene/remove` | POST | Remove non-active scene |
| `/api/scene/manifest/save` | POST | Save manifest to JSON |

### MCP Tools

| Tool | Description |
|------|-------------|
| `list_scenes` | List all registered scenes |
| `get_active_scene` | Get active scene info + reentry data |
| `get_scene` | Get a specific scene definition by ID |
| `transition_scene` | Trigger scene transition |
| `add_scene` | Add scene definition at runtime |
| `update_scene` | Update fields of an existing scene definition |
| `remove_scene` | Remove a non-active scene |
| `save_scene_manifest` | Persist manifest to file |

## Editor Integration

Scene management is NOT a separate "View → Scenes" panel — there is no such menu item. It is a
**"Scenes" collapsing section inside the World Outliner panel** (`editor/src/WorldOutlinerPanel.cpp`,
`renderScenesSection()`):
- Lists all scenes, highlights the active scene in green, tags it `[active]`
- Double-click a non-active scene to switch to it; right-click for a context menu
  (Switch to Scene / Delete Scene / Convert to Multi-Scene Project)
- A "+" button opens a Create Scene popup (type World/Menu/Cutscene, name, DB file)

Selecting a scene (single click) shows its details in the **Properties panel**
(`editor/src/PropertiesPanel.cpp`) — NOT a hover tooltip: type, name, world DB, transition style,
enter/exit script paths (all editable), and the last transition result (from → to scene, timing
ms, error text on failure). **Reentry position is not currently displayed anywhere in the editor UI.**

**Known gap:** `SceneCallbacks::setLoadingScreen` is declared in `SceneManager.h` and invoked by
`SceneManager::update()`, but neither the editor (`Application.cpp`) nor `create_project.py`'s
generated scaffold code actually assigns it — so **no loading-screen overlay currently renders**
during a transition in either host, despite the state machine driving it. The same is true for
`resetLighting`, `clearMusic`, `clearObjectives`, `clearSceneStoryCharacters`, `rebuildNavGrid`, and
`runScript` (`onEnterScript`/`onExitScript`) — only `clearEntities`, `clearNPCs`, and `endDialogue`
are currently wired by both hosts. `locationRegistry->clear()` is the one "Cleared" behavior done
directly by `SceneManager` itself (not via a callback), so it does fire. Until the remaining
callbacks are wired, lighting/music/objectives/scene-local story characters/nav grid are not
actually reset on a scene transition, and `onEnterScript`/`onExitScript` are inert JSON fields.

## GameCallbacks Hooks

Standalone games can override these in their `GameCallbacks` subclass:

```cpp
void onSceneUnload(EngineRuntime& engine, const std::string& sceneId) override;
void onSceneLoad(EngineRuntime& engine, const std::string& sceneId) override;
void onSceneReady(EngineRuntime& engine, const std::string& sceneId) override;
```

## Project Scaffolding

`create_project.py` generates multi-scene aware code when the game definition contains a `"scenes"` array. The generated `GameCallbacks` subclass includes `onSceneLoad`, `onSceneUnload`, and `onSceneReady` overrides, and the `loadGameDefinition()` function delegates to `SceneManager` for multi-scene manifests.

`package_game.py` copies per-scene `.db` files into the output `worlds/` directory.

## Testing

- **Unit tests** (20): `tests/core/SceneSystemTest.cpp` — definition parsing, manifest round-trip, state machine transitions
- **Integration tests**: `tests/integration/SceneIntegrationTest.cpp` — callback ordering, re-entry persistence, multi-hop transitions, event emission
- **Sample**: `samples/game_definitions/multi_scene_demo.json`

## Key Source Files

| File | Purpose |
|------|---------|
| `engine/include/core/SceneDefinition.h` | SceneDefinition, SceneManifest structs |
| `engine/src/core/SceneDefinition.cpp` | JSON parsing |
| `engine/include/core/SceneManager.h` | SceneManager class, SceneCallbacks, SceneState |
| `engine/src/core/SceneManager.cpp` | Transition state machine, unload/load logic |
| `engine/include/core/GameCallbacks.h` | Scene lifecycle virtual methods |
| `engine/src/core/EngineAPIServer.cpp` | HTTP endpoints for scene management |
| `scripts/mcp/phyxel_mcp_server.py` | MCP tool wrappers |
| `editor/src/Application.cpp` | Editor scene panel, loading overlay |
