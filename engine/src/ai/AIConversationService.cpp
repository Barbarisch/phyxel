#include "ai/AIConversationService.h"
#include "ui/DialogueSystem.h"
#include "core/EntityRegistry.h"
#include "scene/NPCEntity.h"
#include "scene/Entity.h"
#include "utils/Logger.h"

#include <thread>

namespace Phyxel {
namespace AI {

AIConversationService::AIConversationService(Story::StoryEngine* story,
                                             Core::EntityRegistry* registry,
                                             UI::DialogueSystem* dialogue)
    : m_story(story)
    , m_registry(registry)
    , m_dialogue(dialogue)
{
}

AIConversationService::~AIConversationService() = default;

bool AIConversationService::initialize(sqlite3* db, const LLMConfig& config) {
    // Create LLM client
    m_llmClient = std::make_unique<LLMClient>(config);

    // Create conversation memory (SQLite-backed)
    m_memory = std::make_unique<ConversationMemory>();
    if (db) {
        if (!m_memory->initialize(db)) {
            LOG_WARN("AI", "ConversationMemory failed to initialize (will work without persistence)");
        }
    }

    // Create context manager
    m_contextManager = std::make_unique<ContextManager>(m_story, m_memory.get(), m_registry);

    m_initialized = true;
    LOG_INFO("AI", "AIConversationService initialized (configured={})", isConfigured());
    return true;
}

bool AIConversationService::startConversation(Scene::NPCEntity* npc,
                                               const std::string& npcId,
                                               const std::string& npcName) {
    if (!m_initialized || !m_dialogue) {
        LOG_ERROR("AI", "AIConversationService not initialized");
        return false;
    }

    if (!isConfigured()) {
        LOG_WARN("AI", "LLM not configured (no API key) — cannot start AI conversation");
        return false;
    }

    // Create the send callback that routes through our pipeline
    auto sendCallback = [this, npcId, npcName](const std::string& playerMessage) {
        // Get player position for spatial context
        glm::vec3 playerPos(0.0f);
        if (m_registry) {
            // Try to find player entity
            auto* playerEntity = m_registry->getEntity("player");
            if (playerEntity) {
                playerPos = playerEntity->getPosition();
            }
        }

        // Record the player's turn
        if (m_memory && m_memory->isInitialized()) {
            m_memory->recordTurn(npcId, "player", playerMessage);
        }

        // Build context and call LLM on a background thread
        std::thread([this, npcId, npcName, playerMessage, playerPos]() {
            // Build full prompt from game state
            auto ctx = m_contextManager->buildContext(npcId, playerMessage, playerPos);

            LOG_DEBUG("AI", "Calling LLM for '{}' (~{} tokens)", npcName, ctx.estimatedTokens);

            // Call the LLM
            auto response = m_llmClient->complete(ctx.messages);

            if (response.ok()) {
                // Record NPC's response
                if (m_memory && m_memory->isInitialized()) {
                    m_memory->recordTurn(npcId, npcId, response.content);

                    // Auto-summarize old turns if conversation has grown long
                    m_memory->autoSummarizeIfNeeded(npcId, m_llmClient.get());
                }

                // Deliver to DialogueSystem (thread-safe)
                if (m_dialogue) {
                    m_dialogue->receiveAIResponse(response.content);
                }

                LOG_DEBUG("AI", "LLM response for '{}': {} tokens in, {} tokens out",
                          npcName, response.inputTokens, response.outputTokens);
            } else {
                LOG_ERROR("AI", "LLM error for '{}': {}", npcName, response.error);
                // Send error message so the UI doesn't hang
                if (m_dialogue) {
                    m_dialogue->receiveAIResponse("[The NPC seems lost in thought...]");
                }
            }
        }).detach();
    };

    return m_dialogue->startAIConversation(npc, npcName, sendCallback);
}

void AIConversationService::setLLMConfig(const LLMConfig& config) {
    if (m_llmClient) {
        m_llmClient->setConfig(config);
    }
}

bool AIConversationService::isConfigured() const {
    return m_llmClient && m_llmClient->isConfigured();
}

} // namespace AI
} // namespace Phyxel
