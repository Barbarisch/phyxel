#include "core/NPCManager.h"
#include "core/NavGrid.h"
#include "core/AStarPathfinder.h"
#include "core/ChunkManager.h"
#include "scene/NPCEntity.h"
#include "scene/AnimatedVoxelCharacter.h"
#include "scene/behaviors/IdleBehavior.h"
#include "scene/behaviors/PatrolBehavior.h"
#include "scene/behaviors/BehaviorTreeBehavior.h"
#include "scene/behaviors/ScheduledBehavior.h"
#include "ai/Schedule.h"
#include "core/EntityRegistry.h"
#include "graphics/LightManager.h"
#include "graphics/DayNightCycle.h"
#include "graphics/AnimationSystem.h"
#include "utils/Logger.h"
#include <limits>

namespace Phyxel {
namespace Core {

NPCManager::~NPCManager() = default;

Scene::NPCEntity* NPCManager::spawnNPC(const std::string& name, const std::string& animFile,
                                        const glm::vec3& position, NPCBehaviorType behaviorType,
                                        const std::vector<glm::vec3>& waypoints,
                                        float walkSpeed, float waitTime,
                                        const Scene::CharacterAppearance& appearance) {
    std::unique_ptr<Scene::NPCBehavior> behavior;

    switch (behaviorType) {
        case NPCBehaviorType::Patrol:
            behavior = std::make_unique<Scene::PatrolBehavior>(waypoints, walkSpeed, waitTime);
            break;
        case NPCBehaviorType::BehaviorTree:
            behavior = std::make_unique<Scene::BehaviorTreeBehavior>();
            break;
        case NPCBehaviorType::Scheduled:
            behavior = std::make_unique<Scene::ScheduledBehavior>(AI::Schedule::defaultSchedule());
            break;
        case NPCBehaviorType::Idle:
        default:
            behavior = std::make_unique<Scene::IdleBehavior>();
            break;
    }

    return spawnNPCWithBehavior(name, animFile, position, std::move(behavior), appearance);
}

Scene::NPCEntity* NPCManager::spawnNPCWithBehavior(const std::string& name, const std::string& animFile,
                                                     const glm::vec3& position,
                                                     std::unique_ptr<Scene::NPCBehavior> behavior,
                                                     const Scene::CharacterAppearance& appearance) {
    if (m_npcs.count(name)) {
        LOG_WARN("NPCManager", "NPC '{}' already exists", name);
        return nullptr;
    }

    if (!m_physicsWorld) {
        LOG_ERROR("NPCManager", "Cannot spawn NPC '{}': PhysicsWorld not set", name);
        return nullptr;
    }

    auto npc = std::make_unique<Scene::NPCEntity>(m_physicsWorld, position, name, animFile, appearance);
    npc->setBehavior(std::move(behavior));

    // Wire pathfinder to PatrolBehavior if available
    if (m_pathfinder) {
        if (auto* patrol = dynamic_cast<Scene::PatrolBehavior*>(npc->getBehavior())) {
            patrol->setPathfinder(m_pathfinder.get());
        }
    }

    // Register with EntityRegistry
    std::string entityId = "npc_" + name;
    if (m_entityRegistry) {
        m_entityRegistry->registerEntity(npc.get(), entityId, "npc");
    }

    // Wire context
    npc->setContext(m_entityRegistry, m_lightManager, m_speechBubbleManager, entityId, m_dayNightCycle, m_locationRegistry, m_chunkManager, m_raycastVisualizer);

    auto* rawPtr = npc.get();
    m_npcs[name] = std::move(npc);

    LOG_INFO("NPCManager", "Spawned NPC '{}' at ({}, {}, {})", name, position.x, position.y, position.z);
    return rawPtr;
}

bool NPCManager::removeNPC(const std::string& name) {
    auto it = m_npcs.find(name);
    if (it == m_npcs.end()) return false;

    // Unregister from EntityRegistry
    std::string entityId = "npc_" + name;
    if (m_entityRegistry) {
        m_entityRegistry->unregisterEntity(entityId);
    }

    // Remove attached light
    if (it->second->getAttachedLightId() >= 0 && m_lightManager) {
        m_lightManager->removeLight(it->second->getAttachedLightId());
    }

    m_npcs.erase(it);
    LOG_INFO("NPCManager", "Removed NPC '{}'", name);
    return true;
}

Scene::NPCEntity* NPCManager::getNPC(const std::string& name) const {
    auto it = m_npcs.find(name);
    return (it != m_npcs.end()) ? it->second.get() : nullptr;
}

std::vector<std::string> NPCManager::getAllNPCNames() const {
    std::vector<std::string> names;
    names.reserve(m_npcs.size());
    for (const auto& [name, _] : m_npcs) {
        names.push_back(name);
    }
    return names;
}

void NPCManager::update(float deltaTime) {
    for (auto& [name, npc] : m_npcs) {
        npc->update(deltaTime);
    }

    // Social simulation tick (runs at reduced frequency)
    m_socialTickTimer -= deltaTime;
    if (m_socialTickTimer <= 0.0f) {
        m_socialTickTimer = SOCIAL_TICK_INTERVAL;

        // Convert real seconds to game hours for social system update
        float deltaHours = 0.0f;
        if (m_dayNightCycle) {
            float dayLen = m_dayNightCycle->getDayLengthSeconds();
            if (dayLen > 0.0f) {
                deltaHours = SOCIAL_TICK_INTERVAL * (24.0f / dayLen) * m_dayNightCycle->getTimeScale();
            }
        }

        if (deltaHours > 0.0f) {
            // Update per-NPC needs and worldview decay
            for (auto& [name, npc] : m_npcs) {
                npc->getNeeds().update(deltaHours);
                npc->getWorldView().update(deltaHours);
            }

            // Decay relationships toward neutral
            m_relationships.update(deltaHours);

            // Build participant list and run social interactions
            std::vector<AI::SocialParticipant> participants;
            for (auto& [name, npc] : m_npcs) {
                AI::SocialParticipant p;
                p.id = name;
                p.position = npc->getPosition();
                p.needs = &npc->getNeeds();
                p.worldView = &npc->getWorldView();
                // Read currentActivity from behavior blackboard if available
                if (auto* btBehavior = dynamic_cast<Scene::BehaviorTreeBehavior*>(npc->getBehavior())) {
                    p.currentActivity = btBehavior->getBlackboard().getString("currentActivity", "Wander");
                }
                participants.push_back(p);
            }
            m_socialSystem.update(deltaHours, participants, m_relationships);
        }
    }
}

void NPCManager::buildNavGrid() {
    if (!m_chunkManager) {
        LOG_WARN("NPCManager", "Cannot build NavGrid: ChunkManager not set");
        return;
    }

    // Determine XZ bounds from all loaded chunk origins
    if (m_chunkManager->chunkMap.empty()) {
        LOG_WARN("NPCManager", "Cannot build NavGrid: no chunks loaded");
        return;
    }

    glm::ivec2 minXZ(std::numeric_limits<int>::max());
    glm::ivec2 maxXZ(std::numeric_limits<int>::min());
    for (const auto& [coord, chunk] : m_chunkManager->chunkMap) {
        glm::ivec3 origin = ChunkManager::chunkCoordToOrigin(coord);
        minXZ.x = std::min(minXZ.x, origin.x);
        minXZ.y = std::min(minXZ.y, origin.z);
        maxXZ.x = std::max(maxXZ.x, origin.x + 31);
        maxXZ.y = std::max(maxXZ.y, origin.z + 31);
    }

    m_navGrid = std::make_unique<NavGrid>(m_chunkManager);
    m_navGrid->buildFromRegion(minXZ, maxXZ);
    m_pathfinder = std::make_unique<AStarPathfinder>(m_navGrid.get());

    // Re-wire all PatrolBehaviors to the new pathfinder and invalidate stale paths.
    // If an NPC is on a nearWall cell (physics body would clip the wall), 
    // relocate it to the nearest safe cell center.
    for (auto& [name, npc] : m_npcs) {
        if (auto* patrol = dynamic_cast<Scene::PatrolBehavior*>(npc->getBehavior())) {
            patrol->setPathfinder(m_pathfinder.get());
            patrol->invalidatePath();
        }

        // Check if NPC is stuck on a nearWall cell and relocate
        glm::vec3 pos = npc->getPosition();
        const NavCell* cell = m_navGrid->getCell(
            static_cast<int>(std::floor(pos.x)),
            static_cast<int>(std::floor(pos.z)));
        if (cell && cell->nearWall) {
            const NavCell* safe = m_navGrid->findNearestNonWall(pos);
            if (safe) {
                glm::vec3 safePos(
                    static_cast<float>(safe->x) + 0.5f,
                    static_cast<float>(safe->surfaceY) + 1.0f,
                    static_cast<float>(safe->z) + 0.5f);
                npc->setPosition(safePos);
                LOG_INFO("NPCManager", "Relocated NPC '{}' from nearWall ({},{}) to ({},{},{})",
                         name, cell->x, cell->z, safePos.x, safePos.y, safePos.z);
            }
        }
    }

    LOG_INFO("NPCManager", "Built NavGrid: XZ [{},{}] to [{},{}], {} cells ({} walkable)",
             minXZ.x, minXZ.y, maxXZ.x, maxXZ.y,
             m_navGrid->cellCount(), m_navGrid->walkableCellCount());
}

void NPCManager::onVoxelChanged(const glm::ivec3& worldPos) {
    if (m_navGrid) {
        m_navGrid->rebuildCell(worldPos.x, worldPos.z);
    }
}

void NPCManager::onRegionChanged(const glm::ivec3& minPos, const glm::ivec3& maxPos) {
    if (m_navGrid) {
        m_navGrid->rebuildRegion(minPos.x, minPos.z, maxPos.x, maxPos.z);
    }
}

const NPCManager::AnimTemplate* NPCManager::getOrLoadTemplate(const std::string& animFile) {
    auto it = m_templateCache.find(animFile);
    if (it != m_templateCache.end()) {
        return &it->second;
    }

    // Load from file
    AnimTemplate tmpl;
    Phyxel::AnimationSystem animSys;
    if (!animSys.loadFromFile(animFile, tmpl.skeleton, tmpl.clips, tmpl.voxelModel)) {
        LOG_ERROR("NPCManager", "Failed to load template anim file: {}", animFile);
        return nullptr;
    }

    LOG_INFO("NPCManager", "Cached anim template '{}' ({} bones, {} shapes, {} clips)",
             animFile, tmpl.skeleton.bones.size(), tmpl.voxelModel.shapes.size(), tmpl.clips.size());

    auto [insertIt, _] = m_templateCache.emplace(animFile, std::move(tmpl));
    return &insertIt->second;
}

Scene::NPCEntity* NPCManager::spawnProceduralNPC(const std::string& name, const std::string& seedAnimFile,
                                                   const glm::vec3& position, NPCBehaviorType behaviorType,
                                                   const std::string& role,
                                                   const std::vector<glm::vec3>& waypoints,
                                                   float walkSpeed, float waitTime,
                                                   const Scene::CharacterAppearance& appearance) {
    if (m_npcs.count(name)) {
        LOG_WARN("NPCManager", "NPC '{}' already exists", name);
        return nullptr;
    }
    if (!m_physicsWorld) {
        LOG_ERROR("NPCManager", "Cannot spawn NPC '{}': PhysicsWorld not set", name);
        return nullptr;
    }

    const AnimTemplate* tmpl = getOrLoadTemplate(seedAnimFile);
    if (!tmpl) {
        return nullptr;
    }

    // Generate appearance: use provided appearance, or auto-generate from name+role
    Scene::CharacterAppearance finalAppearance = appearance;
    if (!role.empty()) {
        auto morph = Scene::detectMorphology(tmpl->skeleton);
        finalAppearance = Scene::CharacterAppearance::generateFromSeed(name, role, morph);
    }

    // Create NPC entity with procedural skeleton (no file re-read)
    auto npc = std::make_unique<Scene::NPCEntity>(m_physicsWorld, position, name, finalAppearance,
                                                   tmpl->skeleton, tmpl->voxelModel, tmpl->clips);

    // Set behavior
    std::unique_ptr<Scene::NPCBehavior> behavior;
    switch (behaviorType) {
        case NPCBehaviorType::Patrol:
            behavior = std::make_unique<Scene::PatrolBehavior>(waypoints, walkSpeed, waitTime);
            break;
        case NPCBehaviorType::BehaviorTree:
            behavior = std::make_unique<Scene::BehaviorTreeBehavior>();
            break;
        case NPCBehaviorType::Scheduled:
            behavior = std::make_unique<Scene::ScheduledBehavior>(AI::Schedule::defaultSchedule());
            break;
        case NPCBehaviorType::Idle:
        default:
            behavior = std::make_unique<Scene::IdleBehavior>();
            break;
    }
    npc->setBehavior(std::move(behavior));

    // Wire pathfinder to PatrolBehavior if available
    if (m_pathfinder) {
        if (auto* patrol = dynamic_cast<Scene::PatrolBehavior*>(npc->getBehavior())) {
            patrol->setPathfinder(m_pathfinder.get());
        }
    }

    // Register with EntityRegistry
    std::string entityId = "npc_" + name;
    if (m_entityRegistry) {
        m_entityRegistry->registerEntity(npc.get(), entityId, "npc");
    }
    npc->setContext(m_entityRegistry, m_lightManager, m_speechBubbleManager, entityId, m_dayNightCycle, m_locationRegistry, m_chunkManager, m_raycastVisualizer);

    auto* rawPtr = npc.get();
    m_npcs[name] = std::move(npc);

    LOG_INFO("NPCManager", "Spawned procedural NPC '{}' (role='{}') at ({}, {}, {})",
             name, role, position.x, position.y, position.z);
    return rawPtr;
}

Scene::NPCEntity* NPCManager::spawnPhysicsNPC(const std::string& name, const std::string& animFile,
                                                const glm::vec3& position, NPCBehaviorType behaviorType,
                                                const std::vector<glm::vec3>& waypoints,
                                                float walkSpeed, float waitTime,
                                                const Scene::CharacterAppearance& appearance) {
    if (m_npcs.count(name)) {
        LOG_WARN("NPCManager", "NPC '{}' already exists", name);
        return nullptr;
    }
    if (!m_physicsWorld) {
        LOG_ERROR("NPCManager", "Cannot spawn NPC '{}': PhysicsWorld not set", name);
        return nullptr;
    }

    auto npc = std::make_unique<Scene::NPCEntity>(m_physicsWorld, position, name, animFile, appearance, true);

    std::unique_ptr<Scene::NPCBehavior> behavior;
    switch (behaviorType) {
        case NPCBehaviorType::Patrol:
            behavior = std::make_unique<Scene::PatrolBehavior>(waypoints, walkSpeed, waitTime);
            break;
        case NPCBehaviorType::BehaviorTree:
            behavior = std::make_unique<Scene::BehaviorTreeBehavior>();
            break;
        case NPCBehaviorType::Scheduled:
            behavior = std::make_unique<Scene::ScheduledBehavior>(AI::Schedule::defaultSchedule());
            break;
        case NPCBehaviorType::Idle:
        default:
            behavior = std::make_unique<Scene::IdleBehavior>();
            break;
    }
    npc->setBehavior(std::move(behavior));

    // Wire pathfinder to PatrolBehavior if available
    if (m_pathfinder) {
        if (auto* patrol = dynamic_cast<Scene::PatrolBehavior*>(npc->getBehavior())) {
            patrol->setPathfinder(m_pathfinder.get());
        }
    }

    std::string entityId = "npc_" + name;
    if (m_entityRegistry) {
        m_entityRegistry->registerEntity(npc.get(), entityId, "npc");
    }
    npc->setContext(m_entityRegistry, m_lightManager, m_speechBubbleManager, entityId, m_dayNightCycle, m_locationRegistry, m_chunkManager, m_raycastVisualizer);

    auto* rawPtr = npc.get();
    m_npcs[name] = std::move(npc);

    LOG_INFO("NPCManager", "Spawned physics NPC '{}' at ({}, {}, {})", name, position.x, position.y, position.z);
    return rawPtr;
}

Scene::NPCEntity* NPCManager::spawnPhysicsProceduralNPC(const std::string& name, const std::string& seedAnimFile,
                                                          const glm::vec3& position, NPCBehaviorType behaviorType,
                                                          const std::string& role,
                                                          const std::vector<glm::vec3>& waypoints,
                                                          float walkSpeed, float waitTime,
                                                          const Scene::CharacterAppearance& appearance) {
    if (m_npcs.count(name)) {
        LOG_WARN("NPCManager", "NPC '{}' already exists", name);
        return nullptr;
    }
    if (!m_physicsWorld) {
        LOG_ERROR("NPCManager", "Cannot spawn NPC '{}': PhysicsWorld not set", name);
        return nullptr;
    }

    const AnimTemplate* tmpl = getOrLoadTemplate(seedAnimFile);
    if (!tmpl) {
        return nullptr;
    }

    Scene::CharacterAppearance finalAppearance = appearance;
    if (!role.empty()) {
        auto morph = Scene::detectMorphology(tmpl->skeleton);
        finalAppearance = Scene::CharacterAppearance::generateFromSeed(name, role, morph);
    }

    auto npc = std::make_unique<Scene::NPCEntity>(m_physicsWorld, position, name, finalAppearance,
                                                   tmpl->skeleton, tmpl->voxelModel, tmpl->clips, true);

    std::unique_ptr<Scene::NPCBehavior> behavior;
    switch (behaviorType) {
        case NPCBehaviorType::Patrol:
            behavior = std::make_unique<Scene::PatrolBehavior>(waypoints, walkSpeed, waitTime);
            break;
        case NPCBehaviorType::BehaviorTree:
            behavior = std::make_unique<Scene::BehaviorTreeBehavior>();
            break;
        case NPCBehaviorType::Scheduled:
            behavior = std::make_unique<Scene::ScheduledBehavior>(AI::Schedule::defaultSchedule());
            break;
        case NPCBehaviorType::Idle:
        default:
            behavior = std::make_unique<Scene::IdleBehavior>();
            break;
    }
    npc->setBehavior(std::move(behavior));

    // Wire pathfinder to PatrolBehavior if available
    if (m_pathfinder) {
        if (auto* patrol = dynamic_cast<Scene::PatrolBehavior*>(npc->getBehavior())) {
            patrol->setPathfinder(m_pathfinder.get());
        }
    }

    std::string entityId = "npc_" + name;
    if (m_entityRegistry) {
        m_entityRegistry->registerEntity(npc.get(), entityId, "npc");
    }
    npc->setContext(m_entityRegistry, m_lightManager, m_speechBubbleManager, entityId, m_dayNightCycle, m_locationRegistry, m_chunkManager, m_raycastVisualizer);

    auto* rawPtr = npc.get();
    m_npcs[name] = std::move(npc);

    LOG_INFO("NPCManager", "Spawned physics procedural NPC '{}' (role='{}') at ({}, {}, {})",
             name, role, position.x, position.y, position.z);
    return rawPtr;
}


} // namespace Core
} // namespace Phyxel
