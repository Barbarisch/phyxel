#include "ai/AISystem.h"
#include "utils/Logger.h"

namespace VulkanCube {
namespace AI {

// ============================================================================
// Construction / Destruction
// ============================================================================

AISystem::AISystem() {
    LOG_INFO("AI", "AISystem created");
}

AISystem::~AISystem() {
    shutdown();
}

// ============================================================================
// Lifecycle
// ============================================================================

bool AISystem::initialize(const GooseConfig& config, bool autoStart) {
    if (m_initialized) {
        LOG_WARN("AI", "AISystem: already initialized");
        return true;
    }

    LOG_INFO("AI", "AISystem: initializing...");

    // Create the GooseBridge
    m_bridge = std::make_unique<GooseBridge>(config);

    // Create the character manager
    m_characterManager = std::make_unique<AICharacterManager>(m_bridge.get());

    // Optionally start the goose-server
    if (autoStart) {
        if (!m_bridge->startServer()) {
            LOG_ERROR("AI", "AISystem: failed to start goose-server");
            // Don't fail completely — user might start server manually
        } else {
            // Register the phyxel MCP extension
            m_bridge->registerPhyxelExtension();
        }
    }

    m_initialized = true;
    LOG_INFO("AI", "AISystem: initialized (server {})",
             m_bridge->isServerRunning() ? "running" : "not started");
    return true;
}

void AISystem::shutdown() {
    if (!m_initialized) return;

    LOG_INFO("AI", "AISystem: shutting down...");

    // Stop story director first
    if (m_storyDirector) {
        m_storyDirector->stop();
        m_storyDirector.reset();
    }

    // Stop all NPC controllers
    if (m_characterManager) {
        m_characterManager->stopAll();
        m_characterManager.reset();
    }

    // Stop the goose-server
    if (m_bridge) {
        m_bridge->stopServer();
        m_bridge.reset();
    }

    m_initialized = false;
    LOG_INFO("AI", "AISystem: shutdown complete");
}

bool AISystem::isReady() const {
    return m_initialized && m_bridge && m_bridge->isServerRunning();
}

// ============================================================================
// Game Loop
// ============================================================================

void AISystem::update(float deltaTime) {
    if (!m_initialized) return;

    // Update character controllers (drains command queue, processes per-NPC)
    if (m_characterManager) {
        m_characterManager->update(deltaTime);
    }

    // Update story director (periodic prompting)
    if (m_storyDirector) {
        m_storyDirector->update(deltaTime);
    }
}

// ============================================================================
// NPC Management
// ============================================================================

AIController* AISystem::createAINPC(Scene::Entity* entity,
                                     const std::string& entityId,
                                     const std::string& recipePath,
                                     const std::string& personality) {
    if (!m_characterManager) return nullptr;

    auto* controller = m_characterManager->createController(
        entity, entityId, recipePath);

    if (controller && !personality.empty()) {
        controller->setPersonality(personality);
    }

    return controller;
}

void AISystem::removeAINPC(const std::string& entityId) {
    if (m_characterManager) {
        m_characterManager->removeController(entityId);
    }
}

AIController* AISystem::getAINPC(const std::string& entityId) {
    return m_characterManager ? m_characterManager->getController(entityId) : nullptr;
}

void AISystem::broadcastEvent(const std::string& eventType,
                               const json& eventData) {
    if (m_characterManager) {
        m_characterManager->broadcastEvent(eventType, eventData);
    }

    // Also notify the story director
    if (m_storyDirector && m_storyDirector->isActive()) {
        m_storyDirector->notifyEvent(eventType, eventData);
    }
}

// ============================================================================
// Story System
// ============================================================================

bool AISystem::startStoryDirector(const std::string& recipePath) {
    if (!m_bridge) return false;

    m_storyDirector = std::make_unique<StoryDirector>(m_bridge.get());
    return m_storyDirector->start(recipePath);
}

void AISystem::stopStoryDirector() {
    if (m_storyDirector) {
        m_storyDirector->stop();
        m_storyDirector.reset();
    }
}

// ============================================================================
// Configuration
// ============================================================================

bool AISystem::setProvider(const std::string& provider, const std::string& model) {
    return m_bridge ? m_bridge->setProvider(provider, model) : false;
}

const GooseConfig& AISystem::getConfig() const {
    static GooseConfig defaultConfig;
    return m_bridge ? m_bridge->getConfig() : defaultConfig;
}

// ============================================================================
// Diagnostics
// ============================================================================

AISystem::AIStats AISystem::getStats() const {
    AIStats stats;
    if (m_bridge) {
        stats.serverRunning = m_bridge->isServerRunning();
        stats.pendingCommands = m_bridge->getCommandQueue().approximateSize();
        auto usage = m_bridge->getTotalTokenUsage();
        stats.totalInputTokens = usage.totalInputTokens;
        stats.totalOutputTokens = usage.totalOutputTokens;
    }
    if (m_characterManager) {
        stats.activeNPCs = m_characterManager->getControllerCount();
    }
    if (m_storyDirector) {
        stats.storyDirectorActive = m_storyDirector->isActive();
    }
    return stats;
}

} // namespace AI
} // namespace VulkanCube
