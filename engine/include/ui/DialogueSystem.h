#pragma once

#include "ui/DialogueData.h"
#include <string>
#include <vector>
#include <functional>
#include <mutex>

namespace Phyxel {

namespace Scene { class NPCEntity; }
namespace Core { class GameEventLog; }

namespace UI {

/// States of the dialogue state machine.
enum class DialogueState {
    Inactive,        ///< No conversation active
    Typing,          ///< Text being revealed character by character
    WaitingForInput, ///< Full text shown, waiting for Enter to advance
    ChoiceSelection, ///< Choices displayed, waiting for 1-4 key
    AITextInput,     ///< AI conversation: player types a message
    AIWaitingForResponse ///< AI conversation: waiting for AI reply
};

/// A single message in an AI conversation history.
struct AIConversationMessage {
    std::string speaker;  ///< "Player" or NPC name
    std::string text;
    std::string emotion;  ///< Only for NPC messages
};

/// Manages RPG-style dialogue conversations with NPCs.
/// Handles typewriter reveal, choice selection, tree traversal,
/// and AI-driven free-text conversations.
class DialogueSystem {
public:
    using ConversationEndCallback = std::function<void(const std::string& npcName,
                                                       const std::string& lastNodeId)>;

    DialogueSystem() = default;

    // === Static dialogue tree methods ===

    /// Start a conversation with an NPC using a dialogue tree.
    bool startConversation(Scene::NPCEntity* npc, const DialogueTree* tree);

    /// Start a conversation with just a name and tree (no NPC entity needed).
    bool startConversation(const std::string& speakerName, const DialogueTree* tree);

    /// Advance dialogue: skip typewriter or go to next node (Enter key).
    void advanceDialogue();

    /// Select a choice by index (0-based). Only valid in ChoiceSelection state.
    void selectChoice(int index);

    /// End the current conversation (Esc key).
    void endConversation();

    /// Update typewriter effect.
    void update(float dt);

    // === AI conversation methods ===

    /// Start an AI-driven conversation with an NPC.
    /// The sendMessage callback is called when the player submits text.
    using AISendCallback = std::function<void(const std::string& playerMessage)>;
    bool startAIConversation(Scene::NPCEntity* npc, const std::string& npcName,
                              AISendCallback sendCallback);

    /// Called by the player to submit their typed message (Enter key in AITextInput state).
    void submitPlayerMessage();

    /// Submit a message programmatically (for API/MCP use).
    void submitAIInput(const std::string& message);

    /// Called from the AI system when the NPC's response arrives.
    /// Thread-safe: can be called from the Goose SSE callback thread.
    void receiveAIResponse(const std::string& text, const std::string& emotion = "");

    // === AI Enhancement (hybrid dialogue) ===

    /// Callback invoked when a tree node is loaded for an AI-enhanced NPC.
    /// The implementation should ask the AI to rephrase the text in character.
    using AIEnhanceCallback = std::function<void(const std::string& nodeId,
                                                  const std::string& nodeText,
                                                  const std::string& speaker,
                                                  const std::string& emotion)>;

    /// Set/clear the AI enhancement callback. When set, tree dialogue nodes
    /// are sent to AI for in-character rephrasing. Static text shows immediately;
    /// if AI responds during the typewriter phase, it replaces the text.
    void setAIEnhanceCallback(AIEnhanceCallback callback) { m_aiEnhanceCallback = std::move(callback); }

    /// Called from AI system when an enhanced version of a node's text arrives.
    /// Thread-safe: can be called from the Goose SSE callback thread.
    void receiveAIEnhancement(const std::string& nodeId, const std::string& text, const std::string& emotion = "");

    // --- State queries ---
    bool isActive() const { return m_state != DialogueState::Inactive; }
    bool isAIConversation() const { return m_isAIMode; }
    DialogueState getState() const { return m_state; }

    /// Speaker name for the current node.
    const std::string& getCurrentSpeaker() const { return m_currentSpeaker; }
    /// Full text of the current node.
    const std::string& getCurrentText() const { return m_currentFullText; }
    /// Revealed portion of text (typewriter effect).
    const std::string& getRevealedText() const { return m_revealedText; }
    /// Current emotion tag.
    const std::string& getCurrentEmotion() const { return m_currentEmotion; }
    /// Available choices (empty if not in ChoiceSelection state or linear node).
    const std::vector<DialogueChoice>& getAvailableChoices() const { return m_availableChoices; }
    /// The NPC entity in conversation (may be nullptr for dynamic dialogues).
    Scene::NPCEntity* getConversationNPC() const { return m_npc; }

    /// AI text input buffer (mutable for ImGui input).
    char* getInputBuffer() { return m_inputBuffer; }
    static constexpr size_t INPUT_BUFFER_SIZE = 256;

    /// AI conversation history.
    const std::vector<AIConversationMessage>& getConversationHistory() const { return m_conversationHistory; }

    // --- Configuration ---
    void setTypingSpeed(float charsPerSecond) { m_typingSpeed = charsPerSecond; }
    float getTypingSpeed() const { return m_typingSpeed; }

    void setConversationEndCallback(ConversationEndCallback callback) {
        m_endCallback = std::move(callback);
    }

    void setGameEventLog(Core::GameEventLog* log) { m_gameEventLog = log; }

private:
    void loadNode(const std::string& nodeId);
    void finishTyping();
    void applyPendingAIResponse();
    void applyPendingEnhancement();

    DialogueState m_state = DialogueState::Inactive;
    const DialogueTree* m_tree = nullptr;
    Scene::NPCEntity* m_npc = nullptr;
    std::string m_npcName;

    // Current node state (static dialogue)
    std::string m_currentNodeId;
    std::string m_currentSpeaker;
    std::string m_currentFullText;
    std::string m_currentEmotion;
    std::string m_revealedText;
    std::vector<DialogueChoice> m_availableChoices;

    // Typewriter
    float m_typingSpeed = 30.0f;
    float m_typingProgress = 0.0f;
    int m_revealedCharCount = 0;

    // AI conversation state
    bool m_isAIMode = false;
    char m_inputBuffer[INPUT_BUFFER_SIZE] = {};
    std::vector<AIConversationMessage> m_conversationHistory;
    AISendCallback m_aiSendCallback;

    // Thread-safe pending AI response (set from SSE thread, consumed on main thread)
    std::mutex m_pendingMutex;
    std::string m_pendingResponseText;
    std::string m_pendingResponseEmotion;
    bool m_hasPendingResponse = false;

    // AI Enhancement (hybrid dialogue)
    AIEnhanceCallback m_aiEnhanceCallback;
    std::mutex m_enhanceMutex;
    std::string m_pendingEnhanceNodeId;
    std::string m_pendingEnhanceText;
    std::string m_pendingEnhanceEmotion;
    bool m_hasPendingEnhancement = false;

    // Callbacks
    ConversationEndCallback m_endCallback;
    Core::GameEventLog* m_gameEventLog = nullptr;
};

} // namespace UI
} // namespace Phyxel
