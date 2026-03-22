#pragma once

#include <string>
#include <vector>
#include <functional>
#include <nlohmann/json.hpp>
#include <glm/glm.hpp>

namespace Phyxel {

// Forward declarations
class ChunkManager;
class ObjectTemplateManager;

namespace Scene {
class Entity;
}

namespace Core {
class NPCManager;
class EntityRegistry;
class GameEventLog;
}

namespace Graphics {
class Camera;
}

namespace UI {
class DialogueSystem;
}

namespace Story {
class StoryEngine;
}

namespace Core {

using json = nlohmann::json;

// ============================================================================
// GameDefinitionLoader
//
// Loads a complete game definition from a single JSON document and orchestrates
// all subsystem setup: world generation, structure placement, player/NPC
// spawning, dialogue, and story engine configuration.
//
// This enables AI agents to describe an entire game in one JSON payload
// and have the engine set it all up atomically.
//
// JSON Schema (all sections optional):
// {
//   "name": "My Game",
//   "description": "...",
//   "version": "1.0",
//   "world": {
//     "type": "Perlin|Flat|Mountains|Caves|City|Random",
//     "seed": 42,
//     "chunks": [{"x":0,"y":0,"z":0}],
//     "from": {"x":-1,"y":0,"z":-1},
//     "to": {"x":1,"y":0,"z":1},
//     "params": {"heightScale":16, "frequency":0.05, ...}
//   },
//   "structures": [
//     {"type":"fill", "from":{x,y,z}, "to":{x,y,z}, "material":"Stone", "hollow":false},
//     {"type":"template", "template":"tree.txt", "position":{x,y,z}, "dynamic":false}
//   ],
//   "player": {
//     "type": "physics|spider|animated",
//     "position": {"x":16,"y":20,"z":16},
//     "id": "player",
//     "animFile": "character.anim"
//   },
//   "camera": {
//     "position": {"x":50,"y":50,"z":50},
//     "yaw": -135,
//     "pitch": -30
//   },
//   "npcs": [
//     {
//       "name": "Guard",
//       "animFile": "character.anim",
//       "position": {"x":10,"y":20,"z":10},
//       "behavior": "idle|patrol",
//       "waypoints": [{"x":10,"y":20,"z":10}],
//       "walkSpeed": 2.0,
//       "waitTime": 2.0,
//       "dialogue": { "id":"...", "startNodeId":"...", "nodes":[...] },
//       "storyCharacter": {
//         "id": "guard", "faction": "town_guard",
//         "agencyLevel": 1,
//         "traits": {"openness":0.5, ...},
//         "goals": [{"id":"protect","description":"...","priority":0.9}]
//       }
//     }
//   ],
//   "story": {
//     "world": { "factions":{}, "locations":{}, "variables":{} },
//     "arcs": [{ "id":"...", "name":"...", "constraintMode":"Guided", "beats":[...] }]
//   }
// }
// ============================================================================

/// Results from loading a game definition.
struct GameDefinitionResult {
    bool success = false;
    std::string error;

    // Counts of what was created
    int chunksGenerated = 0;
    int structuresPlaced = 0;
    int npcsSpawned = 0;
    bool playerSpawned = false;
    bool cameraSet = false;
    bool storyLoaded = false;

    json toJson() const;
};

/// Subsystem pointers needed by the loader. Set by Application before loading.
struct GameSubsystems {
    ChunkManager* chunkManager = nullptr;
    Core::NPCManager* npcManager = nullptr;
    Core::EntityRegistry* entityRegistry = nullptr;
    ObjectTemplateManager* templateManager = nullptr;
    Core::GameEventLog* gameEventLog = nullptr;
    Graphics::Camera* camera = nullptr;
    UI::DialogueSystem* dialogueSystem = nullptr;
    Story::StoryEngine* storyEngine = nullptr;

    /// Callback for spawning player entities (Application-specific).
    /// Takes (type, position, animFile) → returns entity pointer or nullptr.
    using EntitySpawnFn = std::function<Scene::Entity*(const std::string& type, const glm::vec3& pos, const std::string& animFile)>;
    EntitySpawnFn entitySpawner = nullptr;
};

class GameDefinitionLoader {
public:
    /// Load a complete game definition. Must be called on the game loop thread.
    static GameDefinitionResult load(const json& definition, GameSubsystems& subsystems);

    /// Export the current game state as a game definition JSON.
    static json exportDefinition(const GameSubsystems& subsystems);

    /// Validate a game definition without loading it.
    static std::pair<bool, std::string> validate(const json& definition);

private:
    static void loadWorld(const json& worldDef, GameSubsystems& sub, GameDefinitionResult& result);
    static void loadStructures(const json& structures, GameSubsystems& sub, GameDefinitionResult& result);
    static void loadPlayer(const json& playerDef, GameSubsystems& sub, GameDefinitionResult& result);
    static void loadCamera(const json& cameraDef, GameSubsystems& sub, GameDefinitionResult& result);
    static void loadNPCs(const json& npcsDef, GameSubsystems& sub, GameDefinitionResult& result);
    static void loadStory(const json& storyDef, GameSubsystems& sub, GameDefinitionResult& result);
};

} // namespace Core
} // namespace Phyxel
