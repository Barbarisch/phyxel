#include "core/NPCManager.h"
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

namespace Phyxel {
namespace Core {

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

    // Register with EntityRegistry
    std::string entityId = "npc_" + name;
    if (m_entityRegistry) {
        m_entityRegistry->registerEntity(npc.get(), entityId, "npc");
    }

    // Wire context
    npc->setContext(m_entityRegistry, m_lightManager, m_speechBubbleManager, entityId, m_dayNightCycle, m_locationRegistry);

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

    // Register with EntityRegistry
    std::string entityId = "npc_" + name;
    if (m_entityRegistry) {
        m_entityRegistry->registerEntity(npc.get(), entityId, "npc");
    }
    npc->setContext(m_entityRegistry, m_lightManager, m_speechBubbleManager, entityId, m_dayNightCycle, m_locationRegistry);

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

    std::string entityId = "npc_" + name;
    if (m_entityRegistry) {
        m_entityRegistry->registerEntity(npc.get(), entityId, "npc");
    }
    npc->setContext(m_entityRegistry, m_lightManager, m_speechBubbleManager, entityId, m_dayNightCycle, m_locationRegistry);

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

    std::string entityId = "npc_" + name;
    if (m_entityRegistry) {
        m_entityRegistry->registerEntity(npc.get(), entityId, "npc");
    }
    npc->setContext(m_entityRegistry, m_lightManager, m_speechBubbleManager, entityId, m_dayNightCycle, m_locationRegistry);

    auto* rawPtr = npc.get();
    m_npcs[name] = std::move(npc);

    LOG_INFO("NPCManager", "Spawned physics procedural NPC '{}' (role='{}') at ({}, {}, {})",
             name, role, position.x, position.y, position.z);
    return rawPtr;
}


} // namespace Core
} // namespace Phyxel
