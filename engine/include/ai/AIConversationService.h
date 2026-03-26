#pragma once

#include "ai/LLMClient.h"
#include "ai/ContextManager.h"
#include "ai/ConversationMemory.h"

#include <memory>
#include <string>

// Forward declarations
typedef struct sqlite3 sqlite3;

namespace Phyxel {

namespace Story { class StoryEngine; }
namespace Core { class EntityRegistry; }
namespace UI { class DialogueSystem; }
namespace Scene { class NPCEntity; }

namespace AI {

// ============================================================================
// AIConversationService — Wires LLMClient + ContextManager + ConversationMemory
// into the DialogueSystem's AI conversation mode.
//
// Usage:
//   1. Create with dependencies
//   2. Call initialize() with SQLite db handle
//   3. Call startConversation() when player interacts with an AI NPC
//   4. DialogueSystem handles the rest (input, display, callbacks)
// ============================================================================

class AIConversationService {
public:
    AIConversationService(Story::StoryEngine* story,
                          Core::EntityRegistry* registry,
                          UI::DialogueSystem* dialogue);
    ~AIConversationService();

    /// Initialize SQLite tables + LLM client. Call after WorldStorage is ready.
    bool initialize(sqlite3* db, const LLMConfig& config);

    /// Start an AI conversation for the given NPC.
    /// Wires the send callback so player messages go through
    /// ContextManager -> LLMClient -> DialogueSystem.
    bool startConversation(Scene::NPCEntity* npc, const std::string& npcId,
                           const std::string& npcName);

    /// Update LLM config (e.g. player changed API key in settings)
    void setLLMConfig(const LLMConfig& config);

    /// Check if LLM is properly configured (has API key)
    bool isConfigured() const;

    /// Access internals for advanced usage
    LLMClient* getLLMClient() { return m_llmClient.get(); }
    ContextManager* getContextManager() { return m_contextManager.get(); }
    ConversationMemory* getConversationMemory() { return m_memory.get(); }

private:
    Story::StoryEngine* m_story;
    Core::EntityRegistry* m_registry;
    UI::DialogueSystem* m_dialogue;

    std::unique_ptr<LLMClient> m_llmClient;
    std::unique_ptr<ContextManager> m_contextManager;
    std::unique_ptr<ConversationMemory> m_memory;

    bool m_initialized = false;
};

} // namespace AI
} // namespace Phyxel
