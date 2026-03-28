#pragma once

#include "scene/NPCEntity.h"
#include "scene/CharacterAppearance.h"
#include "graphics/Animation.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <glm/glm.hpp>

namespace Phyxel {

namespace Physics { class PhysicsWorld; }
namespace Graphics { class LightManager; }
namespace UI { class SpeechBubbleManager; }
namespace Core { class EntityRegistry; }

namespace Scene {
class NPCBehavior;
}

namespace Core {

/// Behavior type enum for NPC creation.
enum class NPCBehaviorType {
    Idle,
    Patrol
};

/// NPCManager owns NPC entities and manages their lifecycle.
/// NPCs are also registered in the EntityRegistry with type tag "npc".
class NPCManager {
public:
    NPCManager() = default;

    /// Set the physics world for creating NPC physics bodies.
    void setPhysicsWorld(Physics::PhysicsWorld* world) { m_physicsWorld = world; }
    /// Set the entity registry for NPC registration and spatial queries.
    void setEntityRegistry(EntityRegistry* registry) { m_entityRegistry = registry; }
    /// Set the light manager for NPC-attached lights.
    void setLightManager(Graphics::LightManager* lightManager) { m_lightManager = lightManager; }
    /// Set the speech bubble manager for NPC ambient chatter.
    void setSpeechBubbleManager(UI::SpeechBubbleManager* mgr) { m_speechBubbleManager = mgr; }

    /// Spawn an NPC with the given behavior type.
    /// @param name       Unique name for this NPC.
    /// @param animFile   Animation file to load (e.g. "character.anim").
    /// @param position   World spawn position.
    /// @param behaviorType  Behavior to attach.
    /// @param waypoints  For Patrol behavior: ordered waypoints.
    /// @param walkSpeed  For Patrol behavior: movement speed.
    /// @param waitTime   For Patrol behavior: wait time at each waypoint.
    /// @param appearance Character appearance (colors + proportions).
    /// @return Pointer to the spawned NPC, or nullptr on failure.
    Scene::NPCEntity* spawnNPC(const std::string& name, const std::string& animFile,
                               const glm::vec3& position, NPCBehaviorType behaviorType,
                               const std::vector<glm::vec3>& waypoints = {},
                               float walkSpeed = 2.0f, float waitTime = 2.0f,
                               const Scene::CharacterAppearance& appearance = Scene::CharacterAppearance{});

    /// Spawn an NPC with a custom behavior.
    Scene::NPCEntity* spawnNPCWithBehavior(const std::string& name, const std::string& animFile,
                                            const glm::vec3& position,
                                            std::unique_ptr<Scene::NPCBehavior> behavior,
                                            const Scene::CharacterAppearance& appearance = Scene::CharacterAppearance{});

    /// Spawn a procedural NPC using a cached .anim template + unique appearance.
    /// Loads the template .anim file once (cached), then applies per-NPC appearance variations.
    /// If role is non-empty, appearance is auto-generated from name+role via generateFromSeed.
    Scene::NPCEntity* spawnProceduralNPC(const std::string& name, const std::string& seedAnimFile,
                                          const glm::vec3& position, NPCBehaviorType behaviorType,
                                          const std::string& role = "",
                                          const std::vector<glm::vec3>& waypoints = {},
                                          float walkSpeed = 2.0f, float waitTime = 2.0f,
                                          const Scene::CharacterAppearance& appearance = Scene::CharacterAppearance{});

    /// Spawn a physics-driven NPC (active ragdoll with PID-controlled joints).
    /// Uses VoxelCharacter in Physics drive mode. Animations become motor targets.
    Scene::NPCEntity* spawnPhysicsNPC(const std::string& name, const std::string& animFile,
                                       const glm::vec3& position, NPCBehaviorType behaviorType,
                                       const std::vector<glm::vec3>& waypoints = {},
                                       float walkSpeed = 2.0f, float waitTime = 2.0f,
                                       const Scene::CharacterAppearance& appearance = Scene::CharacterAppearance{});

    /// Spawn a physics-driven NPC from a cached template.
    Scene::NPCEntity* spawnPhysicsProceduralNPC(const std::string& name, const std::string& seedAnimFile,
                                                  const glm::vec3& position, NPCBehaviorType behaviorType,
                                                  const std::string& role = "",
                                                  const std::vector<glm::vec3>& waypoints = {},
                                                  float walkSpeed = 2.0f, float waitTime = 2.0f,
                                                  const Scene::CharacterAppearance& appearance = Scene::CharacterAppearance{});

    /// Remove an NPC by name. Returns false if not found.
    bool removeNPC(const std::string& name);

    /// Get an NPC by name. Returns nullptr if not found.
    Scene::NPCEntity* getNPC(const std::string& name) const;

    /// Get all NPC names.
    std::vector<std::string> getAllNPCNames() const;

    /// Get total NPC count.
    size_t getNPCCount() const { return m_npcs.size(); }

    /// Update all NPCs (called from main update loop).
    void update(float deltaTime);

private:
    Physics::PhysicsWorld* m_physicsWorld = nullptr;
    EntityRegistry* m_entityRegistry = nullptr;
    Graphics::LightManager* m_lightManager = nullptr;
    UI::SpeechBubbleManager* m_speechBubbleManager = nullptr;

    /// Owns all NPC entities. Key = NPC name.
    std::unordered_map<std::string, std::unique_ptr<Scene::NPCEntity>> m_npcs;

    /// Cached .anim templates: animFile -> {skeleton, voxelModel, clips}
    struct AnimTemplate {
        Phyxel::Skeleton skeleton;
        Phyxel::VoxelModel voxelModel;
        std::vector<Phyxel::AnimationClip> clips;
    };
    std::unordered_map<std::string, AnimTemplate> m_templateCache;

    /// Load or retrieve a cached anim template.
    const AnimTemplate* getOrLoadTemplate(const std::string& animFile);
};

} // namespace Core
} // namespace Phyxel
