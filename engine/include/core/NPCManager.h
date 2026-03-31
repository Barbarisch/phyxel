#pragma once

#include "scene/NPCEntity.h"
#include "scene/CharacterAppearance.h"
#include "ai/RelationshipManager.h"
#include "ai/SocialInteraction.h"
#include "graphics/Animation.h"
#include "core/NavGrid.h"
#include "core/AStarPathfinder.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <glm/glm.hpp>

namespace Phyxel {

namespace Physics { class PhysicsWorld; }
namespace Graphics { class LightManager; class DayNightCycle; }
namespace UI { class SpeechBubbleManager; }
namespace Core { class EntityRegistry; class LocationRegistry; }
class ChunkManager;
class RaycastVisualizer;

namespace Scene {
class NPCBehavior;
}

namespace Core {

/// Behavior type enum for NPC creation.
enum class NPCBehaviorType {
    Idle,
    Patrol,
    BehaviorTree,    ///< AI-driven via BehaviorTree / UtilityAI
    Scheduled        ///< Schedule-driven: time-aware behavior tree
};

/// NPCManager owns NPC entities and manages their lifecycle.
/// NPCs are also registered in the EntityRegistry with type tag "npc".
class NPCManager {
public:
    NPCManager() = default;
    ~NPCManager();

    /// Set the physics world for creating NPC physics bodies.
    void setPhysicsWorld(Physics::PhysicsWorld* world) { m_physicsWorld = world; }
    /// Set the entity registry for NPC registration and spatial queries.
    void setEntityRegistry(EntityRegistry* registry) { m_entityRegistry = registry; }
    /// Set the light manager for NPC-attached lights.
    void setLightManager(Graphics::LightManager* lightManager) { m_lightManager = lightManager; }
    /// Set the speech bubble manager for NPC ambient chatter.
    void setSpeechBubbleManager(UI::SpeechBubbleManager* mgr) { m_speechBubbleManager = mgr; }
    /// Set the day/night cycle for schedule-aware behaviors.
    void setDayNightCycle(Graphics::DayNightCycle* cycle) { m_dayNightCycle = cycle; }
    /// Set the location registry for NPC navigation.
    void setLocationRegistry(LocationRegistry* registry) { m_locationRegistry = registry; }
    /// Set the chunk manager for NPC line-of-sight raycasting.
    void setChunkManager(ChunkManager* mgr) { m_chunkManager = mgr; }
    /// Set the raycast visualizer for NPC FOV debug cone rendering.
    void setRaycastVisualizer(RaycastVisualizer* viz) { m_raycastVisualizer = viz; }

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

    // --- Social Simulation Subsystems (shared across all NPCs) ---

    /// Pairwise NPC-NPC relationship manager.
    AI::RelationshipManager& getRelationships() { return m_relationships; }
    const AI::RelationshipManager& getRelationships() const { return m_relationships; }

    /// Social interaction system (detects & runs NPC encounters).
    AI::SocialInteractionSystem& getSocialSystem() { return m_socialSystem; }
    const AI::SocialInteractionSystem& getSocialSystem() const { return m_socialSystem; }

    /// Build (or rebuild) the navigation grid from all loaded chunks.
    /// Call after world generation or significant terrain changes.
    void buildNavGrid();

    /// Get the navigation grid (may be null if not built yet).
    NavGrid* getNavGrid() const { return m_navGrid.get(); }

    /// Get the A* pathfinder (may be null if not built yet).
    AStarPathfinder* getPathfinder() const { return m_pathfinder.get(); }

    /// Notify the nav grid that a voxel changed at the given world position.
    void onVoxelChanged(const glm::ivec3& worldPos);

private:
    Physics::PhysicsWorld* m_physicsWorld = nullptr;
    EntityRegistry* m_entityRegistry = nullptr;
    Graphics::LightManager* m_lightManager = nullptr;
    UI::SpeechBubbleManager* m_speechBubbleManager = nullptr;
    Graphics::DayNightCycle* m_dayNightCycle = nullptr;
    LocationRegistry* m_locationRegistry = nullptr;
    ChunkManager* m_chunkManager = nullptr;
    RaycastVisualizer* m_raycastVisualizer = nullptr;

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

    // Navigation / pathfinding
    std::unique_ptr<NavGrid> m_navGrid;
    std::unique_ptr<AStarPathfinder> m_pathfinder;

    // Social simulation (shared)
    AI::RelationshipManager m_relationships;
    AI::SocialInteractionSystem m_socialSystem;
    float m_socialTickTimer = 0.0f;
    static constexpr float SOCIAL_TICK_INTERVAL = 1.0f; // Check social interactions every 1s
};

} // namespace Core
} // namespace Phyxel
